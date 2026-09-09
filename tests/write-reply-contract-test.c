#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

enum {
    SSH_FXP_STATUS = 101,
    SSH_FX_OK = 0,
    SSH_FX_NO_SUCH_FILE = 2,
    SSH_FX_PERMISSION_DENIED = 3,
    SSH_FX_FAILURE = 4,
    SSH_FX_BAD_MESSAGE = 5,
    SSH_FX_NO_CONNECTION = 6,
    SSH_FX_CONNECTION_LOST = 7,
    SSH_FX_OP_UNSUPPORTED = 8,
};

struct request_fixture {
    int error;
    int replied;
    int reply_type;
    int status_parse_ok;
    uint32_t status;
};

static int sftp_error_to_errno(uint32_t error)
{
    switch (error) {
    case SSH_FX_OK:                return 0;
    case SSH_FX_NO_SUCH_FILE:      return ENOENT;
    case SSH_FX_PERMISSION_DENIED: return EACCES;
    case SSH_FX_FAILURE:           return EPERM;
    case SSH_FX_BAD_MESSAGE:       return EBADMSG;
    case SSH_FX_NO_CONNECTION:     return ENOTCONN;
    case SSH_FX_CONNECTION_LOST:   return ECONNABORTED;
    case SSH_FX_OP_UNSUPPORTED:    return EOPNOTSUPP;
    default:                       return EIO;
    }
}

static int write_reply_error(const struct request_fixture *req)
{
    if (req->error)
        return req->error;
    if (!req->replied)
        return -EIO;
    if (req->reply_type != SSH_FXP_STATUS)
        return -EIO;
    if (!req->status_parse_ok)
        return -EIO;
    if (req->status != SSH_FX_OK)
        return -sftp_error_to_errno(req->status);
    return 0;
}

int main(void)
{
    struct request_fixture req = {
        .replied = 1, .reply_type = SSH_FXP_STATUS,
        .status_parse_ok = 1, .status = SSH_FX_OK
    };

    assert(write_reply_error(&req) == 0);

    req.error = -ECONNRESET;
    assert(write_reply_error(&req) == -ECONNRESET);
    req.error = 0;

    req.replied = 0;
    assert(write_reply_error(&req) == -EIO);
    req.replied = 1;

    req.reply_type = 102;
    assert(write_reply_error(&req) == -EIO);
    req.reply_type = SSH_FXP_STATUS;

    req.status_parse_ok = 0;
    assert(write_reply_error(&req) == -EIO);
    req.status_parse_ok = 1;

    req.status = SSH_FX_PERMISSION_DENIED;
    assert(write_reply_error(&req) == -EACCES);

    req.status = 0xffffffffu;
    assert(write_reply_error(&req) == -EIO);

    puts("write-reply-contract-test: PASS");
    return 0;
}
