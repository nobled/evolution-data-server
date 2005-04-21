/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_STORAGE_H__
#define __EXCHANGE_STORAGE_H__

#include "exchange-types.h"
#include "e-storage.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_STORAGE            (exchange_storage_get_type ())
#define EXCHANGE_STORAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_STORAGE, ExchangeStorage))
#define EXCHANGE_STORAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_STORAGE, ExchangeStorageClass))
#define EXCHANGE_IS_STORAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_STORAGE))
#define EXCHANGE_IS_STORAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_STORAGE))

struct _ExchangeStorage {
	EStorage parent;

	ExchangeStoragePrivate *priv;
};

struct _ExchangeStorageClass {
	EStorageClass parent_class;

};

GType             exchange_storage_get_type (void);

EStorage         *exchange_storage_new      (ExchangeAccount *account);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_STORAGE_H__ */
