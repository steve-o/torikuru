/* pipe2 compatibility:
 *
 * http://www.kernel.org/doc/man-pages/online/pages/man2/pipe.2.html
 */

#ifndef __COMPAT_PIPE2_H__
#define __COMPAT_PIPE2_H__
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int pipe2(int pipefd[2], int flags);

#ifdef __cplusplus
}
#endif

#endif /* __COMPAT_PIPE2_H__ */

/* eof */
