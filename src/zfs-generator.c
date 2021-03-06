#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cmdline.h"

/*
 * Check what dataset we need to use and generate a systemd dropin.
 * Detection is based on kernel parameters
 */

/**
 * Get the root options and append zfsutil if needed
 */
int getRootOptions(char **options) {
	char *optionsval;
	int ret;
	char rw;
	size_t len;

	ret = cmdline_getParam("rootflags=", &optionsval);
	if (ret < 0) {
		fprintf(stderr, "Could not get rootflags= parameter. Error %d\n", ret);
		return 1;
	} else if (ret == 1) {
		// No rootflags specified, go with zfsutil
		optionsval = malloc((strlen("zfsutil") + 1) * sizeof(char));
		strcpy(optionsval, "zfsutil");
	} else if (ret != 0) {
		fprintf(stderr, "Unknown thing happened while reading the rootflags= paramter\n");
		return 2;
	}
	len = strlen(optionsval) + 1;
	// Do we already have zfsutil?
	if (strstr(optionsval, "zfsutil") == NULL) {
		len += strlen(",zfsutil");
	}
	// Do we need to switch to ro?
	ret = cmdline_getSwitch("rw", &rw);
	if (ret != 0) {
		fprintf(stderr, "Could not get rw parameter");
	} else if (rw != 1) {
		len += strlen(",ro");
	}
	// Allocate and fill
	*options = malloc(len * sizeof(char));
	strcpy(*options, optionsval);
	if (strstr(optionsval, "zfsutil") == NULL) {
		strcat(*options, ",zfsutil");
	}
	if (rw != 1) {
		strcat(*options, ",ro");
	}
	free(optionsval);
	return 0;
}

/*
 * Check if pool import should be forced
 */
int getForce(char **forceParam) {
	char *forceval;
	int ret;
	char force = 0;

	ret = cmdline_getParam("zfs_force=", &forceval);
	if (ret < 0) {
		fprintf(stderr, "Could not get zfs_force= parameter. Error %d\n", ret);
		return 1;
	} else if (ret == 1) {
		// No zfs_force parameter specified
	} else if (ret != 0) {
		fprintf(stderr, "Unknown thing happened while reading the zfs_force= paremter\n");
		return 2;
	} else {
		if (strcmp(forceval, "1") == 0) {
			force = 1;
		}
	}

	if (force == 0) {
		*forceParam = malloc(sizeof(char));
		strcpy(*forceParam, "");
	} else {
		*forceParam = malloc(4 * sizeof(char));
		strcpy(*forceParam, " -f");
	}
	free(forceval);
	return 0;
}

/*
 * Check if we should ignore the cache file
 */
int getIgnoreCache(char *ignore) {
	char *ignoreval;
	int ret;

	ret = cmdline_getParam("zfs_ignorecache", &ignoreval);
	if (ret < 0) {
		fprintf(stderr, "Could not get zfs_ignorecache= parameter. Error %d\n", ret);
		return 1;
	} else if (ret == 1) {
		*ignore = 0;
		return 0;
	} else if (ret != 0) {
		fprintf(stderr, "Unknown thing happened while reading the ignorecache= paremter\n");
		return 2;
	}
	if (strcmp(ignoreval, "1") == 0) {
		*ignore = 1;
		free(ignoreval);
	}
	return 0;
}

int generateScanUnit(char *directory, const char *targetName, const char *unitName, int ignoreCache/* 1 = yes, 0 = no */, char *forceParam, char *poolName) {
	char *unitpath;
	char *targetpath;
	char *cacheLine;
	FILE *fp;

	// Build paths
	targetpath = malloc((strlen(directory) + strlen(targetName) + strlen(unitName) + 3) * sizeof(char));
	strcpy(targetpath, directory);
	strcat(targetpath, "/");
	strcat(targetpath, targetName);
	unitpath = malloc((strlen(directory) + strlen(unitName) + 2) * sizeof(char));
	strcpy(unitpath, directory);
	strcat(unitpath, "/");
	strcat(unitpath, unitName);
	// Make wants directory
	if (mkdir(targetpath, 0775) < 0 && errno != EEXIST) {
		perror("Can not create unit directory\n");
		free(targetpath);
		free(unitpath);
		return(1);
	}
	// Make symlink
	strcat(targetpath, "/");
	strcat(targetpath, unitName);
	symlink(unitName, targetpath);
	// Check if unit already exists
	if (access(unitpath, R_OK) != -1) {
		perror("Scanning unit file already exists or cannot be accessed\n");
		free(unitpath);
		free(targetpath);
		return 0;
	}
	// Check if we need to ignore the cache file
	if (ignoreCache == 0) {
		cacheLine = malloc((strlen("ConditionPathExists=!/etc/zfs/zpool.cache") + 1) * sizeof(char));
		strcpy(cacheLine, "ConditionPathExists=!/etc/zfs/zpool.cache");
	} else {
		cacheLine = malloc(sizeof(char));
		strcpy(cacheLine, "");
	}
	// Write
	fp = fopen(unitpath, "w");
	if (fp == NULL) {
		perror("Can not write to scanning unit file\n");
		free(unitpath);
		free(cacheLine);
		free(targetpath);
		return 1;
	}
	fprintf(fp, "[Unit]\n\
Description=Import ZFS pools by device scanning\n\
DefaultDependencies=no\n\
Requires=systemd-udev-settle.service\n\
After=systemd-udev-settle.service\n\
After=cryptsetup.target\n\
Before=sysroot.mount\n\
%s\n\
\n\
[Service]\n\
Type=oneshot\n\
RemainAfterExit=yes\n\
ExecStart=/usr/bin/zpool import %s -N -o cachefile=none%s\n", cacheLine, poolName, forceParam);
	fclose(fp);

	free(cacheLine);
	free(unitpath);
	free(targetpath);
	return 0;
}

int generateCacheUnit(char *directory, const char *targetName, const char *unitName, char *forceParam, char *poolName) {
	char *unitpath;
	char *targetpath;
	FILE *fp;

	// Build paths
	targetpath = malloc((strlen(directory) + strlen(targetName) + strlen(unitName) + 3) * sizeof(char));
	strcpy(targetpath, directory);
	strcat(targetpath, "/");
	strcat(targetpath, targetName);
	unitpath = malloc((strlen(directory) + strlen(unitName) + 2) * sizeof(char));
	strcpy(unitpath, directory);
	strcat(unitpath, "/");
	strcat(unitpath, unitName);
	// Make wants directory
	if (mkdir(targetpath, 0775) < 0 && errno != EEXIST) {
		perror("Can not create unit directory\n");
		free(targetpath);
		free(unitpath);
		return(1);
	}
	// Make symlink
	strcat(targetpath, "/");
	strcat(targetpath, unitName);
	symlink(unitName, targetpath);
	// Check if unit already exists
	if (access(unitpath, R_OK) != -1) {
		free(unitpath);
		free(targetpath);
		printf("Caching unit file already exists\n");
		return 0;
	}
	// Write
	fp = fopen(unitpath, "w");
	if (fp == NULL) {
		perror("Cannot write to scanning unit file\n");
		free(unitpath);
		free(targetpath);
		return 1;
	}
	fprintf(fp, "[Unit]\n\
Description=Import ZFS pools by cache file\n\
DefaultDependencies=no\n\
Requires=systemd-udev-settle.service\n\
After=systemd-udev-settle.service\n\
After=cryptsetup.target\n\
Before=sysroot.mount\n\
ConditionPathExists=/etc/zfs/zpool.cache\n\
\n\
[Service]\n\
Type=oneshot\n\
RemainAfterExit=yes\n\
ExecStart=/usr/bin/zpool import %s -N -c /etc/zfs/zpool.cache%s\n", poolName, forceParam);
	fclose(fp);
	free(unitpath);
	free(targetpath);

	return 0;
}

int generateSysrootUnit(char *directory, int bootfs, char *dataset, char *snapshot) {
	char *unitpath;
	FILE *fp;
	char *targetName = "sysroot.mount.d";
	char *unitName = "zfs.conf";
	char *what;
	char *options = NULL;

	// Build paths
	unitpath = malloc((strlen(directory) + strlen(targetName) + strlen(unitName) + 3) * sizeof(char));
	strcpy(unitpath, directory);
	strcat(unitpath, "/");
	strcat(unitpath, targetName);
	// Make dropin directory
	if (mkdir(unitpath, 0775) < 0 && errno != EEXIST) {
		perror("Can not create unit directory\n");
		free(unitpath);
		return(1);
	}
	strcat(unitpath, "/");
	strcat(unitpath, unitName);
	// Check if unit already exists
	if (access(unitpath, R_OK) != -1) {
		perror("Mounting unit file already exists\n");
		free(unitpath);
		return 0;
	}

	// Discover bootfs
	if (bootfs == 1) {
		if (dataset == NULL) {
			what = malloc((strlen("zfs:AUTO") + 1) * sizeof(char));
			strcpy(what, "zfs:AUTO");
		} else {
			what = malloc((strlen("zfs:AUTO:") + strlen(dataset) + 1) * sizeof(char));
			strcpy(what, "zfs:AUTO:");
			strcat(what, dataset);
		}
	} else {
		what = malloc((strlen(dataset) + 1) * sizeof(char));
		strcpy(what, dataset);
	}
	// Handle snapshots
	if (snapshot != NULL) {
		what = realloc(what, (strlen(what) + strlen(snapshot) + 2) * sizeof(char));
		strcat(what, "@");
		strcat(what, snapshot);
	}

	// Get options
	if (getRootOptions(&options) != 0) {
		fprintf(stderr, "Can not get root options\n");
		free(what);
		free(unitpath);
		return 1;
	}

	// Write
	fp = fopen(unitpath, "w");
	if (fp == NULL) {
		perror("Can not write to mounting unit file\n");
		free(unitpath);
		free(options);
		free(what);
		return 1;
	}
	fprintf(fp, "[Mount]\n\
What=%s\n\
Type=initrd_zfs\n\
Options=%s\n", what, options);
	fclose(fp);
	free(what);
	free(options);
	free(unitpath);

	return 0;
}

int main(int argc, char *argv[]) {
	if (argc != 4) {
		fprintf(stderr, "Only to be used as a systemd generator\n");
		exit(1);
	}

	char *root = NULL;
	char *rpool = NULL;
	char ignoreCache = 0;
	char *forceParam = NULL;
	char *poolName;
	char *dataset;
	char *slash;
	char *snap;
	int ret;
	const int systemd_param = 1;
	const char *importTarget = "initrd-root-device.target.wants";
	const char *scanUnitName = "zfs-import-scan.service";
	const char *cacheUnitName = "zfs-import-cache.service";

	ret = cmdline_getParam("root=", &root);
	if (ret < 0) {
		fprintf(stderr, "Could not get root= parameter. Error %d\n", ret);
		exit(1);
	} else if (ret == 1) {
		fprintf(stderr, "No root parameter specified, don't know what to do\n");
		exit(0);
	} else if (ret != 0) {
		fprintf(stderr, "Unknown thing happened while reading the root= paremter\n");
		exit(1);
	}
	// Handle non-ZFS values
	if (strncmp(root, "zfs:", strlen("zfs:")) != 0) {
		printf("root= does not point to anything ZFS-related. Quitting\n");
		exit(0);
	}
	// Check if we need forcing
	// Not doing this earlier because we are not sure if we even need ZFS
	if (getForce(&forceParam) != 0) {
		free(root);
		exit(1);
	}
	if (getIgnoreCache(&ignoreCache) != 0) {
		free(root);
		free(forceParam);
		exit(1);
	}
	// Generate units
	if (strcmp(root, "zfs:AUTO") != 0 && strncmp(root, "zfs:AUTO@", strlen("zfs:AUTO@")) != 0) {
		poolName = malloc((strlen(root) - 4 + 1) * sizeof(char));
		strcpy(poolName, &(root[4]));

		slash = strchr(poolName, '/');
		if (slash != NULL) {
			*slash = '\0';
		}
		slash = strchr(poolName, '@');
		if (slash != NULL) {
			*slash = '\0';
		}
	} else {
		poolName = malloc(3 * sizeof(char));
		strcpy(poolName, "-a");
	}
	if (generateScanUnit(argv[systemd_param], importTarget, scanUnitName, ignoreCache, forceParam, poolName) != 0) {
		free(root);
		free(forceParam);
		free(poolName);
		exit(1);
	}
	if ((ignoreCache == 0) && generateCacheUnit(argv[systemd_param], importTarget, cacheUnitName, forceParam, poolName) != 0) {
		free(root);
		free(forceParam);
		free(poolName);
		exit(1);
	}
	free(forceParam);
	free(poolName);
	// Snapshot
	strtok(root, "@");
	snap = strtok(NULL, "@");
	// Direct dataset
	if (strcmp(root, "zfs:AUTO") != 0) {
		dataset = malloc((strlen(root) - strlen("zfs:") + 1) * sizeof(char));
		strcpy(dataset, &(root[strlen("zfs:")]));
		free(root);

		if (generateSysrootUnit(argv[systemd_param], 0, dataset, snap) != 0) {
			free(dataset);
			fprintf(stderr, "Can not generate sysroot unit\n");
			exit(1);
		}
		free(dataset);
		exit(0);
	}
	// Bootfs
	ret = cmdline_getParam("rpool=", &rpool);
	if (ret < 0) {
		fprintf(stderr, "Could not get rpool= parameter. Error %d\n", ret);
		exit(1);
	} else if (ret == 1) {
		printf("Will use bootfs value of any pool\n");
		if (generateSysrootUnit(argv[systemd_param], 1, NULL, snap) != 0) {
			fprintf(stderr, "Can not generate sysroot unit\n");
			exit(1);
		}
		exit(0);
	} else if (ret != 0) {
		fprintf(stderr, "Unknown thing happened while reading the rpool= paremter\n");
		exit(1);
	}
	printf("Will use bootfs of pool %s\n", rpool);
	if (generateSysrootUnit(argv[systemd_param], 1, rpool, snap) != 0) {
		fprintf(stderr, "Can not generate sysroot unit\n");
		exit(1);
	}

	free(root);
	free(rpool);
}
