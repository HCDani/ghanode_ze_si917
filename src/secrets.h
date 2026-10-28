/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_SECRETS_H_
#define GHANODE_SECRETS_H_

#if __has_include("secret.h")
#include "secret.h"
#else
#warning "secret.h not found: building with the placeholder credentials in secret_example.h"
#include "secret_example.h"
#endif

#endif /* GHANODE_SECRETS_H_ */
