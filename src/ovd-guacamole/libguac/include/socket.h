
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is libguac.
 *
 * The Initial Developer of the Original Code is
 * Michael Jumper.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef _GUAC_SOCKET_H
#define _GUAC_SOCKET_H

#include <stdint.h>
#include <unistd.h>

/**
 * Defines the guac_socket object and functionss for using and manipulating it.
 *
 * @file socket.h
 */


/**
 * The core I/O object of Guacamole. guac_socket provides buffered input and
 * output as well as convenience methods for efficiently writing base64 data.
 */
typedef struct guac_socket {

    /**
     * The file descriptor to be read from / written to.
     */
    int fd; 
    
    /**
     * The number of bytes present in the base64 "ready" buffer.
     */
    int __ready;

    /**
     * The base64 "ready" buffer. Once this buffer is filled, base64 data is
     * flushed to the main write buffer.
     */
    int __ready_buf[3];

    /**
     * The number of bytes currently in the main write buffer.
     */
    int __written;

    /**
     * The main write buffer. Bytes written go here before being flushed
     * to the open file descriptor.
     */
    char __out_buf[8192];

    /**
     * The current location of parsing within the instruction buffer.
     */
    int __instructionbuf_parse_start;

    /**
     * The current size of the instruction buffer.
     */
    int __instructionbuf_size;

    /**
     * The number of bytes currently in the instruction buffer.
     */
    int __instructionbuf_used_length;

    /**
     * The instruction buffer. This is essentially the input buffer,
     * provided as a convenience to be used to buffer instructions until
     * those instructions are complete and ready to be parsed.
     */
    char* __instructionbuf;

    /**
     * The number of elements parsed so far.
     */
    int __instructionbuf_elementc;

    /**
     * Array of pointers into the instruction buffer, where each pointer
     * points to the start of the corresponding element.
     */
    char* __instructionbuf_elementv[64];

} guac_socket;

/**
 * Allocates and initializes a new guac_socket object with the given open
 * file descriptor.
 *
 * If an error occurs while allocating the guac_socket object, NULL is returned,
 * and guac_error is set appropriately.
 *
 * @param fd An open file descriptor that this guac_socket object should manage.
 * @return A newly allocated guac_socket object associated with the given
 *         file descriptor, or NULL if an error occurs while allocating
 *         the guac_socket object.
 */
guac_socket* guac_socket_open(int fd);

/**
 * Writes the given unsigned int to the given guac_socket object. The data
 * written may be buffered until the buffer is flushed automatically or
 * manually.
 *
 * If an error occurs while writing, a non-zero value is returned, and
 * guac_error is set appropriately.
 *
 * @param socket The guac_socket object to write to.
 * @param i The unsigned int to write.
 * @return Zero on success, or non-zero if an error occurs while writing.
 */
ssize_t guac_socket_write_int(guac_socket* socket, int64_t i);

/**
 * Writes the given string to the given guac_socket object. The data
 * written may be buffered until the buffer is flushed automatically or
 * manually. Note that if the string can contain characters used
 * internally by the Guacamole protocol (commas, semicolons, or
 * backslashes) it will need to be escaped.
 *
 * If an error occurs while writing, a non-zero value is returned, and
 * guac_error is set appropriately.
 *
 * @param socket The guac_socket object to write to.
 * @param str The string to write.
 * @return Zero on success, or non-zero if an error occurs while writing.
*/
ssize_t guac_socket_write_string(guac_socket* socket, const char* str);

/**
 * Writes the given binary data to the given guac_socket object as base64-encoded
 * data. The data written may be buffered until the buffer is flushed
 * automatically or manually. Beware that because base64 data is buffered
 * on top of the write buffer already used, a call to guac_socket_flush_base64() must
 * be made before non-base64 writes (or writes of an independent block of
 * base64 data) can be made.
 *
 * If an error occurs while writing, a non-zero value is returned, and
 * guac_error is set appropriately.
 *
 * @param socket The guac_socket object to write to.
 * @param buf A buffer containing the data to write.
 * @param count The number of bytes to write.
 * @return Zero on success, or non-zero if an error occurs while writing.
 */
ssize_t guac_socket_write_base64(guac_socket* socket, const void* buf, size_t count);

/**
 * Flushes the base64 buffer, writing padding characters as necessary.
 *
 * If an error occurs while writing, a non-zero value is returned, and
 * guac_error is set appropriately.
 *
 * @param socket The guac_socket object to flush
 * @return Zero on success, or non-zero if an error occurs during flush.
 */
ssize_t guac_socket_flush_base64(guac_socket* socket);

/**
 * Flushes the write buffer.
 *
 * If an error occurs while writing, a non-zero value is returned, and
 * guac_error is set appropriately.
 *
 * @param socket The guac_socket object to flush
 * @return Zero on success, or non-zero if an error occurs during flush.
 */
ssize_t guac_socket_flush(guac_socket* socket);


/**
 * Waits for input to be available on the given guac_socket object until the
 * specified timeout elapses.
 *
 * If an error occurs while waiting, a negative value is returned, and
 * guac_error is set appropriately.
 *
 * If a timeout occurs while waiting, zero value is returned, and
 * guac_error is set to GUAC_STATUS_INPUT_TIMEOUT.
 *
 * @param socket The guac_socket object to wait for.
 * @param usec_timeout The maximum number of microseconds to wait for data, or
 *                     -1 to potentially wait forever.
 * @return Positive on success, zero if the timeout elapsed and no data is
 *         available, negative on error.
 */
int guac_socket_select(guac_socket* socket, int usec_timeout);

/**
 * Frees resources allocated to the given guac_socket object. Note that this
 * implicitly flush all buffers, but will NOT close the associated file
 * descriptor.
 *
 * @param socket The guac_socket object to close.
 */
void guac_socket_close(guac_socket* socket);

#endif

