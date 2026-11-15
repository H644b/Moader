/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MOADER_DYNAMIC_H
#define MOADER_DYNAMIC_H
#include "lazy.h"
struct lfs_moader_catalogue;
void lfs_moader_poll(void);
void lfs_moader_reload_locked(void);
struct lfs_moader_catalogue *lfs_moader_current(void);
void lfs_moader_retain(struct lfs_moader_catalogue *);
void lfs_moader_release(struct lfs_moader_catalogue *);
const struct lfs_lazy_game *lfs_moader_game(struct lfs_moader_catalogue *,uint64_t);
/* Returned views borrow an owner reference; release it after the entire read. */
const struct lfs_view *lfs_moader_view(void *,struct lfs_life *,const void **);
#endif
