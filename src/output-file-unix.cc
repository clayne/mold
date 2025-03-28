#include "mold.h"

#include <fcntl.h>
#include <filesystem>
#include <sys/file.h>
#include <sys/mman.h>
#include <system_error>

namespace mold {

static u32 get_umask() {
  u32 orig_umask = umask(0);
  umask(orig_umask);
  return orig_umask;
}

template <typename E>
static int
open_or_create_file(Context<E> &ctx, std::string path, std::string tmpfile,
                    int perm) {
  // Reuse an existing file if exists and writable because on Linux,
  // writing to an existing file is much faster than creating a fresh
  // file and writing to it.
  if (ctx.overwrite_output_file && rename(path.c_str(), tmpfile.c_str()) == 0) {
    i64 fd = ::open(tmpfile.c_str(), O_RDWR | O_CREAT, perm);
    if (fd != -1)
      return fd;
    unlink(tmpfile.c_str());
  }

  i64 fd = ::open(tmpfile.c_str(), O_RDWR | O_CREAT, perm);
  if (fd == -1)
    Fatal(ctx) << "cannot open " << tmpfile << ": " << errno_string();
  return fd;
}

template <typename E>
class MemoryMappedOutputFile : public OutputFile<E> {
public:
  MemoryMappedOutputFile(Context<E> &ctx, std::string path, i64 filesize, int perm)
    : OutputFile<E>(path, filesize, true) {
    std::string pid = std::to_string(getpid());
    std::string tmpfile =
      path_dirname(path) / ("." + path_filename(path) + "." + pid);

    this->fd = open_or_create_file(ctx, path, tmpfile, perm);

    if (fchmod(this->fd, perm & ~get_umask()) == -1)
      Fatal(ctx) << "fchmod failed: " << errno_string();

    if (ftruncate(this->fd, filesize) == -1)
      Fatal(ctx) << "ftruncate failed: " << errno_string();

    output_tmpfile = (char *)save_string(ctx, tmpfile).data();

#ifdef __linux__
    fallocate(this->fd, 0, 0, filesize);
#endif

    this->buf = (u8 *)mmap(nullptr, filesize, PROT_READ | PROT_WRITE,
                           MAP_SHARED, this->fd, 0);
    if (this->buf == MAP_FAILED)
      Fatal(ctx) << path << ": mmap failed: " << errno_string();

    mold::output_buffer_start = this->buf;
    mold::output_buffer_end = this->buf + filesize;
  }

  ~MemoryMappedOutputFile() {
    if (fd2 != -1)
      ::close(fd2);
  }

  void close(Context<E> &ctx) override {
    Timer t(ctx, "close_file");

    if (!this->is_unmapped)
      munmap(this->buf, this->filesize);

    if (this->buf2.empty()) {
      ::close(this->fd);
    } else {
      FILE *out = fdopen(this->fd, "w");
      fseek(out, 0, SEEK_END);
      fwrite(&this->buf2[0], this->buf2.size(), 1, out);
      fclose(out);
    }

    // If an output file already exists, open a file and then remove it.
    // This is the fastest way to unlink a file, as it does not make the
    // system to immediately release disk blocks occupied by the file.
    fd2 = ::open(this->path.c_str(), O_RDONLY);
    if (fd2 != -1)
      unlink(this->path.c_str());

    if (rename(output_tmpfile, this->path.c_str()) == -1)
      Fatal(ctx) << this->path << ": rename failed: " << errno_string();
    output_tmpfile = nullptr;
  }

private:
  int fd2 = -1;
};

template <typename E>
std::unique_ptr<OutputFile<E>>
OutputFile<E>::open(Context<E> &ctx, std::string path, i64 filesize, int perm) {
  Timer t(ctx, "open_file");

  if (path.starts_with('/') && !ctx.arg.chroot.empty())
    path = ctx.arg.chroot + "/" + path_clean(path);

  std::error_code error;
  bool is_special = path == "-" ||
                    (!std::filesystem::is_regular_file(path, error) && !error);

  OutputFile<E> *file;
  if (is_special)
    file = new MallocOutputFile(ctx, path, filesize, perm);
  else
    file = new MemoryMappedOutputFile(ctx, path, filesize, perm);

#ifdef MADV_HUGEPAGE
  // Enable transparent huge page for an output memory-mapped file.
  // On Linux, it has an effect only on tmpfs mounted with `huge=advise`,
  // but it can make the linker ~10% faster. You can try it by creating
  // a tmpfs with the following commands
  //
  //  $ mkdir tmp
  //  $ sudo mount -t tmpfs -o size=2G,huge=advise none tmp
  //
  // and then specifying a path under the directory as an output file.
  madvise(file->buf, filesize, MADV_HUGEPAGE);
#endif

  if (ctx.arg.filler != -1)
    memset(file->buf, ctx.arg.filler, filesize);
  return std::unique_ptr<OutputFile>(file);
}

// LockingOutputFile is similar to MemoryMappedOutputFile, but it doesn't
// rename output files and instead acquires file lock using flock().
template <typename E>
LockingOutputFile<E>::LockingOutputFile(Context<E> &ctx, std::string path,
                                        int perm)
  : OutputFile<E>(path, 0, true) {
  this->fd = ::open(path.c_str(), O_RDWR | O_CREAT, perm);
  if (this->fd == -1)
    Fatal(ctx) << "cannot open " << path << ": " << errno_string();
  flock(this->fd, LOCK_EX);

  // We may be overwriting to an existing debug info file. We want to
  // make the file unusable so that gdb won't use it by accident until
  // it's ready.
  u8 buf[256] = {};
  (void)!!write(this->fd, buf, sizeof(buf));
}

template <typename E>
void LockingOutputFile<E>::resize(Context<E> &ctx, i64 filesize) {
  if (ftruncate(this->fd, filesize) == -1)
    Fatal(ctx) << "ftruncate failed: " << errno_string();

  this->buf = (u8 *)mmap(nullptr, filesize, PROT_READ | PROT_WRITE,
                         MAP_SHARED, this->fd, 0);
  if (this->buf == MAP_FAILED)
    Fatal(ctx) << this->path << ": mmap failed: " << errno_string();

  this->filesize = filesize;
  mold::output_buffer_start = this->buf;
  mold::output_buffer_end = this->buf + filesize;
}

template <typename E>
void LockingOutputFile<E>::close(Context<E> &ctx) {
  if (!this->is_unmapped)
    munmap(this->buf, this->filesize);

  if (!this->buf2.empty()) {
    FILE *out = fdopen(this->fd, "w");
    fseek(out, 0, SEEK_END);
    fwrite(&this->buf2[0], this->buf2.size(), 1, out);
    fclose(out);
  }

  ::close(this->fd);
}

using E = MOLD_TARGET;

template class OutputFile<E>;
template class LockingOutputFile<E>;

} // namespace mold
