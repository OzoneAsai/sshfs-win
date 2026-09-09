/*
 * Admission control for new server-side handles. Caller holds sshfs.lock.
 * Count both live handles and OPEN/OPENDIR requests that have been admitted
 * but have not completed yet, so concurrent opens cannot overshoot the cap.
 */
static int remote_handle_reserve_locked(struct request *req, uint8_t type,
					int want_reply)
{
	struct conn *conn = req->conn;

	if (!want_reply || sshfs.handle_limit == 0)
		return 0;
	if (type != SSH_FXP_OPEN && type != SSH_FXP_OPENDIR)
		return 0;
	if (conn->remote_handles + conn->remote_handle_reservations >=
	    sshfs.handle_limit)
		return -EMFILE;

	conn->remote_handle_reservations++;
	req->remote_handle_reserved = 1;
	req->remote_handle_reservation_connver = conn->connver;
	return 0;
}

/*
 * Reservation ownership belongs to the request until either its OPEN reply is
 * accounted or connection teardown cancels the request. Caller holds
 * sshfs.lock. Clearing the per-request bit makes those paths idempotent.
 */
static void remote_handle_release_reservation_locked(struct request *req)
{
	struct conn *conn;

	if (!req->remote_handle_reserved)
		return;

	conn = req->conn;
	if (req->remote_handle_reservation_connver == conn->connver) {
		assert(conn->remote_handle_reservations != 0);
		conn->remote_handle_reservations--;
	}
	req->remote_handle_reserved = 0;
}

/*
 * Track SFTP protocol handles, not Windows/FUSE file objects.
 *
 * A successful OPEN/OPENDIR creates one server-side handle and a successful
 * CLOSE retires one. A failed CLOSE deliberately leaves the count unchanged:
 * this makes the value a conservative upper bound while the session remains
 * alive. When the SSH connection is destroyed the server necessarily drops
 * every handle in that session, so process_requests() resets the current
 * count there.
 *
 * This observer never closes, reopens or evicts a handle. It exists to
 * measure pressure before changing file identity semantics.
 */
static void remote_handle_account(struct request *req, uint8_t type, int err)
{
	struct conn *conn;
	unsigned old_count;
	unsigned new_count;
	unsigned peak;
	unsigned old_bucket = 0;
	unsigned new_bucket = 0;
	unsigned conn_index;
	int warn = 0;

	if (type != SSH_FXP_OPEN && type != SSH_FXP_OPENDIR &&
	    type != SSH_FXP_CLOSE)
		return;

	conn = req->conn;
	pthread_mutex_lock(&sshfs.lock);
	remote_handle_release_reservation_locked(req);
	old_count = conn->remote_handles;
	if (err == 0 && (type == SSH_FXP_OPEN || type == SSH_FXP_OPENDIR)) {
		conn->remote_handles++;
		if (conn->remote_handles > conn->remote_handle_peak)
			conn->remote_handle_peak = conn->remote_handles;
		if (sshfs.handle_warn != 0) {
			old_bucket = old_count / sshfs.handle_warn;
			new_bucket = conn->remote_handles / sshfs.handle_warn;
			warn = new_bucket > old_bucket;
		}
	} else if (err == 0 && type == SSH_FXP_CLOSE &&
		   conn->remote_handles != 0) {
		conn->remote_handles--;
	}
	new_count = conn->remote_handles;
	peak = conn->remote_handle_peak;
	conn_index = (unsigned) (conn - sshfs.conns);
	pthread_mutex_unlock(&sshfs.lock);

	if (warn)
		fprintf(stderr,
			"sshfs: remote-handle-observer connection=%u "
			"tracked=%u peak=%u warning_step=%u\n",
			conn_index,
			new_count,
			peak,
			sshfs.handle_warn);
}
