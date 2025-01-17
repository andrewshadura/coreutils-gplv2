/* openat-style fd-relative functions for SE Linux
   Copyright (C) 2007 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* written by Jim Meyering */

#include <config.h>

#include "selinux-at.h"
#include "openat.h"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "dirname.h" /* solely for definition of IS_ABSOLUTE_FILE_NAME */
#include "save-cwd.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include "openat-priv.h"

#define AT_FUNC_NAME getfileconat
#define AT_FUNC_F1 getfilecon
#define AT_FUNC_F2 getfilecon
#define AT_FUNC_USE_F1_COND 1
#define AT_FUNC_POST_FILE_PARAM_DECLS , security_context_t *con
#define AT_FUNC_POST_FILE_ARGS        , con
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_F2
#undef AT_FUNC_USE_F1_COND
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

#define AT_FUNC_NAME lgetfileconat
#define AT_FUNC_F1 lgetfilecon
#define AT_FUNC_F2 lgetfilecon
#define AT_FUNC_USE_F1_COND 1
#define AT_FUNC_POST_FILE_PARAM_DECLS , security_context_t *con
#define AT_FUNC_POST_FILE_ARGS        , con
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_F2
#undef AT_FUNC_USE_F1_COND
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

#define AT_FUNC_NAME setfileconat
#define AT_FUNC_F1 setfilecon
#define AT_FUNC_F2 setfilecon
#define AT_FUNC_USE_F1_COND 1
#define AT_FUNC_POST_FILE_PARAM_DECLS , security_context_t con
#define AT_FUNC_POST_FILE_ARGS        , con
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_F2
#undef AT_FUNC_USE_F1_COND
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS

#define AT_FUNC_NAME lsetfileconat
#define AT_FUNC_F1 lsetfilecon
#define AT_FUNC_F2 lsetfilecon
#define AT_FUNC_USE_F1_COND 1
#define AT_FUNC_POST_FILE_PARAM_DECLS , security_context_t con
#define AT_FUNC_POST_FILE_ARGS        , con
#include "at-func.c"
#undef AT_FUNC_NAME
#undef AT_FUNC_F1
#undef AT_FUNC_F2
#undef AT_FUNC_USE_F1_COND
#undef AT_FUNC_POST_FILE_PARAM_DECLS
#undef AT_FUNC_POST_FILE_ARGS
