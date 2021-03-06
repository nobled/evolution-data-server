/* Evolution calendar - iCalendar file backend for tasks
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Rodrigo Moya <rodrigo@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "e-cal-backend-file-journal.h"

G_DEFINE_TYPE (
	ECalBackendFileJournal,
	e_cal_backend_file_journal,
	E_TYPE_CAL_BACKEND_FILE)

static void
e_cal_backend_file_journal_class_init (ECalBackendFileJournalClass *klass)
{
}

static void
e_cal_backend_file_journal_init (ECalBackendFileJournal *cbfile)
{
	e_cal_backend_file_set_file_name (
		E_CAL_BACKEND_FILE (cbfile), "journal.ics");
}

