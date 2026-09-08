#include "fileio.h"
#include "vm.h"
#include "output.h"
#include "hv_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

/* -------------------------------------------------------------- registry */

static char **shared_paths;
static int    shared_count;

void fileio_set_shared(char **paths, int n)
{
	shared_paths = paths;
	shared_count = n;
}

static const char *path_basename(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

/* A guest opens a shared file by name; -f gives us a path. Match on basename. */
static const char *resolve_shared(const char *name)
{
	int i;

	for (i = 0; i < shared_count; i++) {
		if (!strcmp(path_basename(shared_paths[i]), name))
			return shared_paths[i];
	}

	return NULL;
}

/* ------------------------------------------------------------ validation */

int is_valid_name(const char *name)
{
	size_t i;

	if (!name || name[0] == '\0')
		return 0;

	if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z')))
		return 0;

	for (i = 1; name[i] != '\0'; i++) {
		char c = name[i];

		if (i >= GUEST_NAME_MAX - 1)
			return 0;

		/*
			The spec lists letters, digits and a dot. The underscore is a
			deliberate extension: the course's own test programs name their
			files b1_data.txt and the like. It cannot express a path, so the
			traversal argument (design D4) is unchanged.
		*/
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '.' || c == '_'))
			return 0;
	}

	return 1;
}

/* ------------------------------------------------------------- fd table */

static struct guest_file *alloc_fd(struct vm *v, int *fd_out)
{
	int i;

	for (i = 0; i < MAX_OPEN_FILES; i++) {
		if (!v->files[i].used) {
			memset(&v->files[i], 0, sizeof(v->files[i]));
			v->files[i].used = 1;
			v->files[i].hostfd = -1;
			*fd_out = i;
			return &v->files[i];
		}
	}

	return NULL;
}

/* Guest fds are indices into this VM's own table; anything else is refused. */
static struct guest_file *get_file(struct vm *v, int32_t fd)
{
	if (fd < 0 || fd >= MAX_OPEN_FILES)
		return NULL;

	if (!v->files[fd].used)
		return NULL;

	return &v->files[fd];
}

static void free_fd(struct guest_file *f)
{
	if (f->hostfd >= 0)
		close(f->hostfd);

	memset(f, 0, sizeof(*f));
	f->hostfd = -1;
}

/* --------------------------------------------------------------- helpers */

static int can_read(const struct guest_file *f)
{
	return (f->flags & (HV_O_RD | HV_O_RDWR)) != 0;
}

static int can_write(const struct guest_file *f)
{
	return (f->flags & (HV_O_WR | HV_O_RDWR)) != 0;
}

/*
	Spec flags are not POSIX flags and must never be handed to open(2).

	Documented decisions:
	  - O_RD|O_WR is treated as O_RDWR.
	  - O_CREATE with no access flag is an error: a descriptor you can neither
	    read nor write is not useful, and silently picking one for the guest
	    would hide the mistake.
*/
static int to_posix_flags(int hv_flags, int *out)
{
	int readable = (hv_flags & (HV_O_RD | HV_O_RDWR)) != 0;
	int writable = (hv_flags & (HV_O_WR | HV_O_RDWR)) != 0;
	int posix;

	if (!readable && !writable)
		return -1;

	if (readable && writable)
		posix = O_RDWR;
	else if (writable)
		posix = O_WRONLY;
	else
		posix = O_RDONLY;

	if (hv_flags & HV_O_CREATE)
		posix |= O_CREAT;

	*out = posix;
	return 0;
}

/* ---------------------------------------------------------- per-VM setup */

int fileio_vm_init(struct vm *v)
{
	char dir[HOST_PATH_MAX];
	int i;

	for (i = 0; i < MAX_OPEN_FILES; i++)
		v->files[i].hostfd = -1;

	snprintf(dir, sizeof(dir), "vm_%d", v->id);

	if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
		out_printf(v->id, "cannot create %s: %s\n", dir, strerror(errno));
		return -1;
	}

	return 0;
}

void fileio_vm_destroy(struct vm *v)
{
	int i;

	for (i = 0; i < MAX_OPEN_FILES; i++) {
		if (v->files[i].used)
			free_fd(&v->files[i]);
	}
}

/* ------------------------------------------------------------ operations */

static int32_t hv_open(struct vm *v, uint32_t path_gva, int32_t flags)
{
	const char *name = guest_str(v, path_gva, GUEST_NAME_MAX);
	const char *shared;
	struct guest_file *f;
	int posix_flags, fd, hostfd;
	char path[HOST_PATH_MAX];

	if (!name || !is_valid_name(name))
		return -1;

	if (to_posix_flags(flags, &posix_flags) < 0)
		return -1;

	shared = resolve_shared(name);
	if (shared) {
		/*
			A shared file is opened read-only from the registry path. A write
			does not fail: B.10 materializes a private copy first. Creating a
			file that already exists as shared is refused outright.
		*/
		if (flags & HV_O_CREATE)
			return -1;

		if (strlen(shared) >= sizeof(path))
			return -1;

		snprintf(path, sizeof(path), "%s", shared);
		posix_flags = O_RDONLY;
	} else {
		snprintf(path, sizeof(path), "vm_%d/%s", v->id, name);
	}

	/*
		Claim the table slot before touching the filesystem. Opening first meant
		that when the table was full, an O_CREATE open had already created the
		file on disk and then returned -1 to the guest - a side effect from a
		call the guest was told had failed.
	*/
	f = alloc_fd(v, &fd);
	if (!f)
		return -1;

	hostfd = open(path, posix_flags, 0644);
	if (hostfd < 0) {
		free_fd(f);
		return -1;
	}

	snprintf(f->name, sizeof(f->name), "%s", name);
	snprintf(f->host_path, sizeof(f->host_path), "%s", path);
	f->hostfd = hostfd;
	f->off    = 0;             /* spec: the offset is 0 when the file is opened */
	f->flags  = flags;
	f->shared = shared != NULL;
	f->cow    = 0;

	return fd;
}

static int32_t hv_close(struct vm *v, int32_t fd)
{
	struct guest_file *f = get_file(v, fd);

	if (!f)
		return -1;

	free_fd(f);

	return 0;
}

static int32_t hv_read(struct vm *v, int32_t fd, uint32_t buf_gva, uint32_t count)
{
	struct guest_file *f = get_file(v, fd);
	void *buf;
	ssize_t n;

	if (!f || !can_read(f))
		return -1;

	buf = guest_ptr(v, buf_gva, count);
	if (!buf)
		return -1;

	if (count == 0)
		return 0;

	/* D3: our own offset, never the host fd cursor. */
	n = pread(f->hostfd, buf, count, (off_t)f->off);
	if (n < 0)
		return -1;

	f->off += n;

	return (int32_t)n;   /* a short read is reported honestly */
}

/*
	Copy-on-write.

	Copies the shared original into vm_<id>/<name>, reopens it read-write and
	swaps it in. Called from hv_write only, on the first write, and idempotent
	afterwards.

	The subtle part is what is NOT done here: f->off is never touched. Because
	all I/O goes through pread/pwrite against our own offset (design D3), a
	guest that seeks to 500 and then writes still writes at 500 after the
	underlying host fd has been replaced. Had the offset lived in the kernel's
	file description, it would have reset here and the corruption would have
	been silent.

	The original is only ever read, so it is provably untouched.
*/
static int cow_materialize(struct vm *v, struct guest_file *f)
{
	char local[HOST_PATH_MAX];
	char buf[8192];
	int src = -1, dst = -1, rw = -1;
	ssize_t n;

	if (f->cow)
		return 0;

	if (snprintf(local, sizeof(local), "vm_%d/%s", v->id, f->name) >= (int)sizeof(local))
		return -1;

	/*
		The copy may already exist: a guest can hold two descriptors on the same
		shared file, and each triggers copy-on-write independently. Copying
		again would truncate the private file and silently discard whatever was
		written through the other descriptor, so an existing local copy is
		adopted rather than recreated.
	*/
	rw = open(local, O_RDWR);
	if (rw >= 0)
		goto adopt;

	src = open(f->host_path, O_RDONLY);
	if (src < 0) {
		out_printf(v->id, "cow: cannot read %s: %s\n", f->host_path, strerror(errno));
		return -1;
	}

	dst = open(local, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (dst < 0) {
		out_printf(v->id, "cow: cannot create %s: %s\n", local, strerror(errno));
		close(src);
		return -1;
	}

	while ((n = read(src, buf, sizeof(buf))) > 0) {
		ssize_t written = 0;

		while (written < n) {
			ssize_t w = write(dst, buf + written, (size_t)(n - written));

			if (w <= 0) {
				out_printf(v->id, "cow: write to %s failed: %s\n", local, strerror(errno));
				close(src);
				close(dst);
				return -1;
			}
			written += w;
		}
	}

	if (n < 0) {
		out_printf(v->id, "cow: read of %s failed: %s\n", f->host_path, strerror(errno));
		close(src);
		close(dst);
		return -1;
	}

	close(src);
	close(dst);

	rw = open(local, O_RDWR);
	if (rw < 0) {
		out_printf(v->id, "cow: cannot reopen %s: %s\n", local, strerror(errno));
		return -1;
	}

adopt:
	close(f->hostfd);
	f->hostfd = rw;
	f->cow    = 1;
	snprintf(f->host_path, sizeof(f->host_path), "%s", local);
	/* f->off deliberately left alone. */

	out_printf(v->id, "cow: %s is now private\n", f->name);

	return 0;
}

static int32_t hv_write(struct vm *v, int32_t fd, uint32_t buf_gva, uint32_t count)
{
	struct guest_file *f = get_file(v, fd);
	const void *buf;
	ssize_t n;

	if (!f || !can_write(f))
		return -1;

	buf = guest_ptr(v, buf_gva, count);
	if (!buf)
		return -1;

	if (count == 0)
		return 0;

	/* Spec: the copy is made on the first write to a shared file. */
	if (f->shared && !f->cow && cow_materialize(v, f) < 0)
		return -1;

	n = pwrite(f->hostfd, buf, count, (off_t)f->off);
	if (n < 0)
		return -1;

	f->off += n;

	return (int32_t)n;
}

static int32_t hv_lseek(struct vm *v, int32_t fd, int32_t offset, int32_t whence)
{
	struct guest_file *f = get_file(v, fd);
	struct stat st;

	if (!f)
		return -1;

	switch (whence) {
	case HV_SEEK_SET:
		if (offset < 0)
			return -1;
		f->off = offset;
		break;
	case HV_SEEK_END:
		/* Spec: the offset value is ignored for this indicator. */
		if (fstat(f->hostfd, &st) < 0)
			return -1;
		f->off = (long)st.st_size;
		break;
	default:
		return -1;
	}

	return (int32_t)f->off;
}

/* ----------------------------------------------------------- dispatcher */

int hv_file_request(struct vm *v, uint32_t req_gpa)
{
	struct hv_request *req = guest_ptr(v, req_gpa, sizeof(*req));

	if (!req) {
		/*
			The address came from the guest's own stack, so this means the guest
			is broken. Report it and hand back -1 rather than terminating: the
			guest can still see the failure through its next IN.
		*/
		out_printf(v->id, "file request at out-of-range address 0x%x\n", req_gpa);
		v->last_ret = -1;
		return 0;
	}

	switch (req->op) {
	case HV_OPEN:
		req->ret = hv_open(v, req->arg0, (int32_t)req->arg1);
		break;
	case HV_CLOSE:
		req->ret = hv_close(v, (int32_t)req->arg0);
		break;
	case HV_READ:
		req->ret = hv_read(v, (int32_t)req->arg0, req->arg1, req->arg2);
		break;
	case HV_WRITE:
		req->ret = hv_write(v, (int32_t)req->arg0, req->arg1, req->arg2);
		break;
	case HV_LSEEK:
		req->ret = hv_lseek(v, (int32_t)req->arg0, (int32_t)req->arg1, (int32_t)req->arg2);
		break;
	default:
		out_printf(v->id, "unknown file op %u\n", req->op);
		req->ret = -1;
		break;
	}

	v->last_ret = req->ret;

	return 0;
}
