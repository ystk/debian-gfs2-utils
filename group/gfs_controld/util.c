#include "gfs_daemon.h"
#include "config.h"
#include "libfenced.h"

void update_flow_control_status(void)
{
	cpg_flow_control_state_t flow_control_state;
	cpg_error_t error;

	error = cpg_flow_control_state_get(cpg_handle_daemon, &flow_control_state);
	if (error != CPG_OK) {
		log_error("cpg_flow_control_state_get %d", error);
		return;
	}

	if (flow_control_state == CPG_FLOW_CONTROL_ENABLED) {
		if (libcpg_flow_control_on == 0) {
			log_debug("flow control on");
		}
		libcpg_flow_control_on = 1;
	} else {
		if (libcpg_flow_control_on) {
			log_debug("flow control off");
		}
		libcpg_flow_control_on = 0;
	}
}

int we_are_in_fence_domain(void)
{
	struct fenced_node nodeinfo;
	int rv;

	memset(&nodeinfo, 0, sizeof(nodeinfo));

	rv = fenced_node_info(our_nodeid, &nodeinfo);
	if (rv < 0) {
		log_debug("fenced_node_info error %d", rv);
		return 0;
	}

	if (nodeinfo.member)
		return 1;
	return 0;
}

#define SYSFS_DIR       "/sys/fs"
#define SYSFS_BUFLEN    64

int set_sysfs(struct mountgroup *mg, const char *field, int val)
{
	char fname[PATH_MAX];
	char out[SYSFS_BUFLEN];
	int rv, fd;

	snprintf(fname, PATH_MAX, "%s/%s/%s/lock_module/%s",
		 SYSFS_DIR, mg->mount_args.type, mg->mount_args.table, field);

	log_group(mg, "set %s to %d", fname, val);

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		log_group(mg, "set open %s error %d %d", fname, fd, errno);
		return -1;
	}

	memset(out, 0, sizeof(out));
	sprintf(out, "%d", val);

	rv = write(fd, out, strlen(out));
	if (rv < 0)
		log_group(mg, "set write %s error %d", fname, errno);
	close(fd);

	return 0;
}

int run_dmsetup_suspend(struct mountgroup *mg, char *dev)
{
	struct sched_param sched_param;
	char fname[PATH_MAX];
	char smajor[16];
	char sminor[16];
	pid_t pid;
	int i;
	int major, minor;
	FILE *fp;

	snprintf(fname, PATH_MAX, "%s/%s/%s/device/dev",
		 SYSFS_DIR, mg->mount_args.type, mg->mount_args.table);

	fp = fopen(fname, "r");
	if (fp == NULL) {
		log_group(mg, "set open %s error %d", fname, errno);
		return -1;
	}

	if (fscanf(fp, "%d:%d", &major, &minor) != 2) {
		log_group(mg, "cannot read device numbers %d", errno);
		return -1;
	}

	fclose(fp);

	log_group(mg, "run_dmsetup_suspend %d:%d", major, minor);

	snprintf(smajor, 16, "%d", major);
	snprintf(sminor, 16, "%d", minor);

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid) {
		mg->dmsetup_wait = 1;
		mg->dmsetup_pid = pid;
		return 0;
	} else {
		sched_param.sched_priority = 0;
		sched_setscheduler(0, SCHED_OTHER, &sched_param);

		for (i = 0; i < 50; i++)
			close(i);

		execlp("dmsetup", "dmsetup", "suspend",  "--nolockfs",
		       "--noflush", "-j", smajor, "-m", sminor, NULL);
		exit(EXIT_FAILURE);
	}
	return -1;
}

static void dmsetup_suspend_done(struct mountgroup *mg, int rv)
{
	log_group(mg, "dmsetup_suspend_done result %d", rv);
	mg->dmsetup_wait = 0;
	mg->dmsetup_pid = 0;

	if (!rv) {
		mg->withdraw_suspend = 1;
		send_withdraw(mg);
	}
}

void update_dmsetup_wait(void)
{
	struct mountgroup *mg;
	int status;
	int waiting = 0;
	pid_t pid;

	list_for_each_entry(mg, &mountgroups, list) {
		if (mg->dmsetup_wait) {
			pid = waitpid(mg->dmsetup_pid, &status, WNOHANG);

			/* process not exited yet */
			if (!pid) {
				waiting++;
				continue;
			}

			if (pid < 0) {
				log_error("update_dmsetup_wait %s: waitpid %d "
					  "error %d", mg->name,
					  mg->dmsetup_pid, errno);
				dmsetup_suspend_done(mg, -2);
				continue;
			}

			/* process exited */

			if (!WIFEXITED(status) || WEXITSTATUS(status))
				dmsetup_suspend_done(mg, -1);
			else
				dmsetup_suspend_done(mg, 0);
		}
	}

	if (!waiting) {
		dmsetup_wait = 0;
		log_debug("dmsetup_wait off");
	}
}

static int ignore_nolock(const char *sysfs_dir, char *table)
{
	char path[PATH_MAX];
	int fd;

	memset(path, 0, PATH_MAX);

	snprintf(path, PATH_MAX, "%s/%s/lock_module/proto_name",
		 sysfs_dir, table);

	/* lock_nolock doesn't create the "lock_module" dir at all,
	   so we'll fail to open this */

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 1;

	close(fd);
	return 0;
}

/* This is for the case where gfs_controld exits/fails, abandoning gfs
   filesystems in the kernel, and then gfs_controld is restarted.  When
   gfs_controld exits and abandons lockspaces, that node needs to be
   rebooted to clear the uncontrolled filesystems from the kernel. */

int check_uncontrolled_filesystems(void)
{
	DIR *d;
	struct dirent *de;
	int count = 0;

	d = opendir("/sys/fs/gfs/");
	if (!d)
		goto gfs2;

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

		if (ignore_nolock("/sys/fs/gfs/", de->d_name))
			continue;

		log_error("found uncontrolled gfs fs %s", de->d_name);
		count++;
	}
	closedir(d);

 gfs2:
	d = opendir("/sys/fs/gfs2/");
	if (!d)
		goto out;

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

		if (ignore_nolock("/sys/fs/gfs2/", de->d_name))
			continue;

		log_error("found uncontrolled gfs2 fs %s", de->d_name);
		count++;
	}
	closedir(d);

 out:
	if (count) {
		kick_node_from_cluster(our_nodeid);
		return -1;
	}
	return 0;
}

