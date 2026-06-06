#ifdef __ANDROID__
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* vcpkg static OpenSSL may reference glibc fortify symbols absent on some API levels. */
ssize_t __sendto_chk(int fd, const void *buf, size_t len, int flags,
                     const struct sockaddr *addr, socklen_t addrlen,
                     size_t func_len)
{
    (void)func_len;
    return sendto(fd, buf, len, flags, addr, addrlen);
}
#endif
