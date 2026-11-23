#include <cerrno>
#include <cstdlib>
#include <malloc.h>
#include <unistd.h>
#include <switch.h>

extern "C" int posix_memalign(void **result, size_t alignment, size_t size)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
        return EINVAL;

    const int saved_errno = errno;
    void *ptr = memalign(alignment, size);
    errno = saved_errno;
    if (!ptr)
        return ENOMEM;

    *result = ptr;
    return 0;
}

extern "C" lldiv_t lldiv(long long numerator, long long denominator)
{
    lldiv_t result = { numerator / denominator, numerator % denominator };
    return result;
}

extern "C" long sysconf(int name)
{
    if (name == _SC_PAGESIZE)
        return 0x1000;

    if (name == _SC_PHYS_PAGES)
    {
        u64 memory = 0;
        if (R_SUCCEEDED(svcGetInfo(&memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
            return static_cast<long>(memory / 0x1000);

        errno = EIO;
        return -1;
    }

    errno = EINVAL;
    return -1;
}
