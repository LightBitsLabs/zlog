/*
 * This file is part of the zlog Library.
 *
 * Copyright (C) 2011 by Hardy Simpson <HardySimpson1984@gmail.com>
 *
 * The zlog Library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The zlog Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the zlog Library. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "conf.h"
#include "rule.h"
#include "format.h"
#include "level.h"
#include "zc_defs.h"

/*******************************************************************************/
static void zlog_conf_debug(zlog_conf_t * a_conf);
#define ZLOG_CONF_DEFAULT_FORMAT "&default   \"%d(%F %T) %V [%p:%F:%L] %m%n\""
#define ZLOG_CONF_DEFAULT_RULE "*.*        >stdout"
#define ZLOG_CONF_DEFAULT_BUF_SIZE_MIN 1024
#define ZLOG_CONF_DEFAULT_BUF_SIZE_MAX (2 * 1024 * 1024)
#define ZLOG_CONF_DEFAULT_ROTATE_LOCK_FILE "/tmp/zlog.lock"
/*******************************************************************************/
static int zlog_conf_parse_line(zlog_conf_t * a_conf, char *line, long line_len)
{
	int rc = 0;
	int nread = 0;
	int value_start;
	char name[MAXLEN_CFG_LINE + 1];
	char value[MAXLEN_CFG_LINE + 1];
	zlog_format_t *a_format = NULL;
	zlog_rule_t *a_rule = NULL;

	if (line_len > MAXLEN_CFG_LINE || strlen(line) > MAXLEN_CFG_LINE) {
		zc_error ("line_len[%ld] > MAXLEN_CFG_LINE[%ld], may cause overflow",
		     line_len, MAXLEN_CFG_LINE);
		return -1;
	}

	zc_debug("line=[%s]", line);

	switch (line[0]) {
	case '@':
		memset(name, 0x00, sizeof(name));
		memset(value, 0x00, sizeof(value));
		nread = sscanf(line, "@%s %n%s", name, &value_start, value);
		if (nread != 2) {
			zc_error("sscanf [%s] fail, name or value is null",
				 line);
			return -1;
		}

		if (STRCMP(name, ==, "ignore_error_format_rule")) {
			if (STRICMP(value, ==, "true")) {
				a_conf->ignore_error_format_rule = 1;
			} else {
				a_conf->ignore_error_format_rule = 0;
			}
			zc_debug("ignore_error_format_rule=[%d]",
				a_conf->ignore_error_format_rule);
		} else if (STRCMP(name, ==, "buf_size_min")) {
			a_conf->buf_size_min = zc_parse_byte_size(value);
			zc_debug("buf_size_min=[%ld]", a_conf->buf_size_min);
		} else if (STRCMP(name, ==, "buf_size_max")) {
			a_conf->buf_size_max = zc_parse_byte_size(value);
			zc_debug("buf_size_max=[%ld]", a_conf->buf_size_max);
		} else if (STRCMP(name, ==, "rotate_lock_file")) {
			if (strlen(value) >
			    sizeof(a_conf->rotate_lock_file) - 1) {
				zc_error("lock_file name[%s] is too long",
					 value);
				return -1;
			}
			strcpy(a_conf->rotate_lock_file, value);
			zc_debug("lock_file=[%s]", a_conf->rotate_lock_file);
		} else if (STRCMP(name, ==, "default_format")) {
			int nwrite;
			char format_string[MAXLEN_CFG_LINE + 1];

			nwrite = snprintf(format_string, sizeof(format_string),
				"&default %s", line + value_start);
			if (nwrite < 0 || nwrite >= sizeof(a_conf->file)) {
				zc_error("not enough space for format_string, nwrite=[%d]", nwrite);
				return -1;
			}

			if (a_conf->default_format) {
				zlog_format_del(a_conf->default_format);
			}

			a_conf->default_format = zlog_format_new(format_string,
				strlen(format_string));
			if (!a_conf->default_format) {
				zc_error("new default format fail, [%s]", format_string);
				return -1;
			}

			zc_debug("overwrite inner default_format, [%s]", format_string);
		} else if (STRCMP(name, ==, "level")) {
			char str[MAXLEN_CFG_LINE + 1];
			int level;
			char syslog_level[MAXLEN_CFG_LINE + 1];
			nread = sscanf(line + value_start, " %[^= ] = %d ,%s",
				str, &level, syslog_level);
			if (nread < 2) {
				zc_error("level[%s] syntax wrong", line);
				return -1;
			}
			rc = zlog_level_set(str, level, syslog_level);
			if (rc) {
				zc_error("zlog_level_set fail");
				return -1;
			}
		} else {
			zc_error("in line[%s], name[%s] is wrong", line, name);
			return -1;
		}
		break;
	case '&':
		a_format = zlog_format_new(line, strlen(line));
		if (!a_format) {
			if (getenv("ZLOG_CHECK_FORMAT_RULE")) {
				zc_error("zlog_format_new fail [%s]", line);
				return -1;
			} else if (a_conf->ignore_error_format_rule) {
				zc_error("ignore_error_format [%s]", line);
				return 0;
			} else {
				zc_error("zlog_format_new fail [%s]", line);
				return -1;
			}
		}
		rc = zc_arraylist_add(a_conf->formats, a_format);
		if (rc) {
			zc_error("add list fail");
			zlog_format_del(a_format);
			return -1;
		}
		break;
	default:
		a_rule = zlog_rule_new(a_conf->default_format,
			a_conf->formats, line, strlen(line));
		if (!a_rule) {
			if (getenv("ZLOG_CHECK_FORMAT_RULE")) {
				zc_error("zlog_rule_new fail [%s]", line);
				return -1;
			} else if (a_conf->ignore_error_format_rule) {
				zc_error("ignore_error_rule [%s]", line);
				return 0;
			} else {
				zc_error("zlog_rule_new fail [%s]", line);
				return -1;
			}
		}
		rc = zc_arraylist_add(a_conf->rules, a_rule);
		if (rc) {
			zc_error("add list fail");
			zlog_rule_del(a_rule);
			return -1;
		}
		break;
	}

	return 0;
}

static int zlog_conf_read_config(zlog_conf_t * a_conf)
{
	int rc = 0;
	FILE *fp = NULL;

	char line[MAXLEN_CFG_LINE + 1];
	char *pline = NULL;
	char *p = NULL;
	int line_no = 0;
	size_t line_len = 0;
	int i = 0;

	struct stat a_stat;
	struct tm local_time;

	rc = lstat(a_conf->file, &a_stat);
	if (rc) {
		zc_error("lstat conf file[%s] fail, errno[%d]", a_conf->file,
			 errno);
		return -1;
	}

	localtime_r(&(a_stat.st_mtime), &local_time);
	strftime(a_conf->mtime, sizeof(a_conf->mtime), "%F %X", &local_time);

	if ((fp = fopen(a_conf->file, "r")) == NULL) {
		zc_error("open configure file[%s] fail", a_conf->file);
		return -1;
	}
	/* Now process the file.
	 */
	pline = line;
	memset(&line, 0x00, sizeof(line));
	while (fgets((char *)pline, sizeof(line) - (pline - line), fp) != NULL) {
		++line_no;
		line_len = strlen(pline);
		if (pline[line_len - 1] == '\n') {
			pline[line_len - 1] = '\0';
		}

		/* check for end-of-section, comments, strip off trailing
		 * spaces and newline character.
		 */
		p = pline;
		while (*p && isspace((int)*p))
			++p;
		if (*p == '\0' || *p == '#')
			continue;

		for (i = 0; p[i] != '\0'; ++i) {
			pline[i] = p[i];
		}
		pline[i] = '\0';

		for (p = pline + strlen(pline) - 1; isspace((int)*p); --p)
			/*EMPTY*/;

		if (*p == '\\') {
			if ((p - line) > MAXLEN_CFG_LINE - 30) {
				/* Oops the buffer is full - what now? */
				pline = line;
			} else {
				for (p--; isspace((int)*p); --p)
					/*EMPTY*/;
				p++;
				*p = 0;
				pline = p;
				continue;
			}
		} else
			pline = line;

		*++p = '\0';

		/* we now have the complete line,
		 * and are positioned at the first non-whitespace
		 * character. So let's process it
		 */
		rc = zlog_conf_parse_line(a_conf, line, strlen(line));
		if (rc) {
			zc_error("parse configure file[%s] line[%ld] fail",
				 a_conf->file, line_no);
			goto zlog_conf_read_config_exit;
		}
	}

      zlog_conf_read_config_exit:

	fclose(fp);
	return rc;
}

static int zlog_conf_build(zlog_conf_t * a_conf)
{
	int rc = 0;
	zlog_rule_t *a_rule;

	/* set default configuration start */
	a_conf->ignore_error_format_rule = 0;
	a_conf->buf_size_min = ZLOG_CONF_DEFAULT_BUF_SIZE_MIN;
	a_conf->buf_size_max = ZLOG_CONF_DEFAULT_BUF_SIZE_MAX;
	strcpy(a_conf->rotate_lock_file, ZLOG_CONF_DEFAULT_ROTATE_LOCK_FILE);
	a_conf->default_format = zlog_format_new(ZLOG_CONF_DEFAULT_FORMAT,
			    strlen(ZLOG_CONF_DEFAULT_FORMAT));
	if (!a_conf->default_format) {
		zc_error("zlog_format_new fail");
		rc = -1;
		goto zlog_conf_build_exit;
	}
	/* set default configuration end */

	a_conf->formats =
	    zc_arraylist_new((zc_arraylist_del_fn) zlog_format_del);
	if (!(a_conf->formats)) {
		zc_error("init format_list fail");
		rc = -1;
		goto zlog_conf_build_exit;
	}

	a_conf->rules = zc_arraylist_new((zc_arraylist_del_fn) zlog_rule_del);
	if (!(a_conf->rules)) {
		zc_error("init rule_list fail");
		rc = -1;
		goto zlog_conf_build_exit;
	}

	if (a_conf->file[0] != '\0') {
		rc = zlog_conf_read_config(a_conf);
		if (rc) {
			zc_error("zlog_conf_read_config fail");
			rc = -1;
			goto zlog_conf_build_exit;
		}
	} else {
		a_rule = zlog_rule_new(a_conf->default_format,
				a_conf->formats,
				ZLOG_CONF_DEFAULT_RULE,
				strlen(ZLOG_CONF_DEFAULT_RULE)
			);
		if (!a_rule) {
			zc_error("zlog_rule_new fail");
			goto zlog_conf_build_exit;
		}

		/* add default rule */
		rc = zc_arraylist_add(a_conf->rules, a_rule);
		if (rc) {
			zc_error("zc_arraylist_add fail");
			zlog_rule_del(a_rule);
			rc = -1;
			goto zlog_conf_build_exit;
		}
	}

      zlog_conf_build_exit:
	if (rc) {
		zlog_conf_fini(a_conf);
	} else {
		zlog_conf_debug(a_conf);
	}
	
	return rc;
}

int zlog_conf_init(zlog_conf_t * a_conf, char *conf_file)
{
	int rc = 0;
	int nwrite = 0;

	zc_assert_debug(a_conf, -1);

	if (conf_file) {
		nwrite =
		    snprintf(a_conf->file, sizeof(a_conf->file), "%s",
			     conf_file);
	} else if (getenv("ZLOG_CONF_PATH") != NULL) {
		nwrite =
		    snprintf(a_conf->file, sizeof(a_conf->file), "%s",
			     getenv("ZLOG_CONF_PATH"));
	} else {
		memset(a_conf->file, 0x00, sizeof(a_conf->file));
	}
	if (nwrite < 0 || nwrite >= sizeof(a_conf->file)) {
		zc_error("not enough space for path name, nwrite=[%d]", nwrite);
		return -1;
	}

	rc = zlog_conf_build(a_conf);
	if (rc) {
		zc_error("zlog_conf_build fail");
	}

	return rc;
}

void zlog_conf_fini(zlog_conf_t * a_conf)
{
	zc_assert_debug(a_conf,);

	if (a_conf->default_format) {
		zlog_format_del(a_conf->default_format);
		a_conf->default_format = NULL;
	}
	if (a_conf->formats) {
		zc_arraylist_del(a_conf->formats);
		a_conf->formats = NULL;
	}
	if (a_conf->rules) {
		zc_arraylist_del(a_conf->rules);
		a_conf->rules = NULL;
	}

	memset(a_conf, 0x00, sizeof(zlog_conf_t));
	return;
}

int zlog_conf_update(zlog_conf_t * a_conf, char *conf_file)
{
	int rc = 0;
	int nwrite = 0;
	zlog_conf_t b_conf;

	zc_assert_debug(a_conf, -1);

	memset(&b_conf, 0x00, sizeof(b_conf));

	if (conf_file) {
		nwrite = snprintf(b_conf.file, sizeof(b_conf.file), "%s",
			     conf_file);
	} else if (a_conf->file[0] != '\0') {
		/* use last conf file */
		nwrite = snprintf(b_conf.file, sizeof(b_conf.file), "%s",
			     a_conf->file);
	}
	if (nwrite < 0 || nwrite >= sizeof(b_conf.file)) {
		zc_error("not enough space for path name, nwrite=[%d]",
			 nwrite);
		return -1;
	}

	rc = zlog_conf_build(&b_conf);
	if (rc) {
		/* not change the last conf */
		zc_error("zlog_conf_build fail, use last conf setting");
		return -1;
	} else {
		/* all success, then copy, keep consistency */
		zlog_conf_fini(a_conf);
		memcpy(a_conf, &b_conf, sizeof(b_conf));
		zc_debug("zlog_conf_update succ, use file[%s]", a_conf->file);
	}

	return rc;
}

/*******************************************************************************/
static void zlog_conf_debug(zlog_conf_t * a_conf)
{
	zc_debug("---conf---");
	zc_debug("file:[%s],mtime:[%s]", a_conf->file, a_conf->mtime);
	zc_debug("ignore_error_format_rule:[%d]", a_conf->ignore_error_format_rule);
	zc_debug("buf_size_min:[%ld]", a_conf->buf_size_min);
	zc_debug("buf_size_max:[%ld]", a_conf->buf_size_max);
	zc_debug("rotate_lock_file:[%s]", a_conf->rotate_lock_file);
	zc_debug("formats:[%p]", a_conf->formats);
	zc_debug("rules:[%p]", a_conf->rules);
	return;
}

void zlog_conf_profile(zlog_conf_t * a_conf)
{
	int i;
	zlog_rule_t *a_rule;
	zlog_format_t *a_format;

	zc_error("---conf[%p]---", a_conf);
	zc_error("file:[%s],mtime:[%s]", a_conf->file, a_conf->mtime);
	zc_error("ignore_error_format_rule:[%d]", a_conf->ignore_error_format_rule);
	zc_error("buf_size_min:[%ld]", a_conf->buf_size_min);
	zc_error("buf_size_max:[%ld]", a_conf->buf_size_max);
	zc_error("rotate_lock_file:[%s]", a_conf->rotate_lock_file);

	zc_error("default_format:");
	zlog_format_profile(a_conf->default_format);

	zc_error("---rules[%p]---", a_conf->rules);
	zc_arraylist_foreach(a_conf->rules, i, a_rule) {
		zlog_rule_profile(a_rule);
	}

	zc_error("---formats[%p]---", a_conf->formats);
	zc_arraylist_foreach(a_conf->formats, i, a_format) {
		zlog_format_profile(a_format);
	}
	return;
}