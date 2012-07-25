#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>

int switch_to_user (const char *user)
{
	struct passwd pwbuf;
	struct passwd *pwbufp;
	char *buf;
	size_t buflen;

	buflen = sysconf (_SC_GETPW_R_SIZE_MAX);
	if (buflen < 0) {
		perror ("sysconf");
		return -1;
	}

	buf = (char *) malloc (buflen);
	if (!buf) {
		fprintf (stderr, "Couldn't allocate memory.\n");
		return -1;
	}

	/* Get password file entry for the user. */
	if ((getpwnam_r (user, &pwbuf, buf, buflen, &pwbufp) != 0) || (!pwbufp)) {
		free (buf);

		fprintf (stderr, "Couldn't get password file entry for user: %s.\n", user);
		return -1;
	}

	if (setgid (pwbufp->pw_gid) < 0) {
		perror ("setgid");

		free (buf);

		return -1;
	}

	if (setuid (pwbufp->pw_uid) < 0) {
		perror ("setuid");

		free (buf);

		return -1;
	}

	free (buf);

	return 0;
}
