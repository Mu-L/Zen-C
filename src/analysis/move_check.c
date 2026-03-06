#include "move_check.h"
#include "../diagnostics/diagnostics.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern ParserContext *g_parser_ctx;

#include "zprep.h"

MoveState *move_state_create(MoveState *parent)
{
    MoveState *s = xmalloc(sizeof(MoveState));
    s->entries = NULL;
    s->parent = parent;
    return s;
}

MoveState *move_state_clone(MoveState *src)
{
    if (!src)
    {
        return NULL;
    }
    MoveState *new_state = move_state_create(src->parent);

    MoveEntry *curr = src->entries;
    MoveEntry **tail = &new_state->entries;
    while (curr)
    {
        MoveEntry *new_entry = xmalloc(sizeof(MoveEntry));
        new_entry->symbol_name = xstrdup(curr->symbol_name);
        new_entry->status = curr->status;
        new_entry->moved_at = curr->moved_at;
        new_entry->next = NULL;
        *tail = new_entry;
        tail = &new_entry->next;
        curr = curr->next;
    }
    return new_state;
}

void move_state_free(MoveState *state)
{
    if (!state)
    {
        return;
    }
    MoveEntry *e = state->entries;
    while (e)
    {
        MoveEntry *next = e->next;
        free(e->symbol_name);
        free(e);
        e = next;
    }
    free(state);
}

void mark_moved_in_state(MoveState *state, const char *name, Token t)
{
    if (!state)
    {
        return;
    }

    MoveEntry *e = state->entries;
    while (e)
    {
        if (strcmp(e->symbol_name, name) == 0)
        {
            e->status = MOVE_STATE_MOVED;
            e->moved_at = t;
            return;
        }
        e = e->next;
    }

    MoveEntry *new_entry = xmalloc(sizeof(MoveEntry));
    new_entry->symbol_name = xstrdup(name);
    new_entry->status = MOVE_STATE_MOVED;
    new_entry->moved_at = t;
    new_entry->next = state->entries;
    state->entries = new_entry;
}

MoveStatus get_move_status(MoveState *state, const char *name)
{
    MoveState *s = state;
    while (s)
    {
        MoveEntry *e = s->entries;
        while (e)
        {
            if (strcmp(e->symbol_name, name) == 0)
            {
                return e->status;
            }
            e = e->next;
        }
        s = s->parent;
    }
    return MOVE_STATE_VALID;
}

void move_state_merge(MoveState *target, MoveState *a, MoveState *b)
{
    if (!target)
    {
        return;
    }

    if (a)
    {
        MoveEntry *e = a->entries;
        while (e)
        {
            MoveStatus status_a = e->status;
            MoveStatus status_b = b ? get_move_status(b, e->symbol_name) : MOVE_STATE_VALID;

            if (status_a == MOVE_STATE_MOVED || status_b == MOVE_STATE_MOVED)
            {
                mark_moved_in_state(target, e->symbol_name, e->moved_at);
            }
            e = e->next;
        }
    }

    if (b)
    {
        MoveEntry *e = b->entries;
        while (e)
        {
            MoveStatus status_b = e->status;
            MoveStatus status_a = a ? get_move_status(a, e->symbol_name) : MOVE_STATE_VALID;

            if (status_b == MOVE_STATE_MOVED || status_a == MOVE_STATE_MOVED)
            {
                mark_moved_in_state(target, e->symbol_name, e->moved_at);
            }
            e = e->next;
        }
    }
}

int is_type_copy(ParserContext *ctx, Type *t)
{
    if (!t)
    {
        return 1; // Default to Copy for unknown types
    }

    switch (t->kind)
    {
    case TYPE_INT:
    case TYPE_I8:
    case TYPE_I16:
    case TYPE_I32:
    case TYPE_I64:
    case TYPE_U8:
    case TYPE_U16:
    case TYPE_U32:
    case TYPE_U64:
    case TYPE_F32:
    case TYPE_F64:
    case TYPE_BOOL:
    case TYPE_CHAR:
    case TYPE_VOID:
    case TYPE_POINTER:
    case TYPE_FUNCTION:
    case TYPE_ENUM:
    case TYPE_BITINT:
    case TYPE_UBITINT:
        return 1;

    case TYPE_STRUCT:
        if (check_impl(ctx, "Copy", t->name))
        {
            return 1;
        }
        if (!find_struct_def(ctx, t->name) && !check_impl(ctx, "Drop", t->name))
        {
            return 1;
        }
        return 0;

    case TYPE_ARRAY:
        return is_type_copy(ctx, t->inner);

    case TYPE_ALIAS:
        if (t->alias.is_opaque_alias)
        {
            return 1;
        }
        return is_type_copy(ctx, t->inner);

    default:
        return 1;
    }
}

static void tc_error_with_hints(TypeChecker *tc, Token t, const char *msg, const char *const *hints)
{
    if (tc)
    {
        if (tc->move_checks_only)
        {
            return;
        }
        zerror_with_hints(t, msg, hints);
        tc->error_count++;
    }
    else
    {
        zpanic_with_hints(t, msg, hints);
    }
}

void check_use_validity(TypeChecker *tc, ASTNode *var_node, ZenSymbol *sym)
{
    if (!sym || !var_node)
    {
        return;
    }

    // Check Flow-Sensitive State
    ParserContext *ctx = tc ? tc->pctx : (g_parser_ctx);
    MoveStatus status = MOVE_STATE_VALID;

    if (ctx && ctx->move_state)
    {
        status = get_move_status(ctx->move_state, sym->name);
    }
    else
    {
        if (sym->is_moved)
        {
            status = MOVE_STATE_MOVED;
        }
    }

    if (status == MOVE_STATE_MOVED || status == MOVE_STATE_MAYBE_MOVED)
    {
        char msg[256];
        snprintf(msg, 255, "Use of moved value '%s'", sym->name);

        const char *hints[] = {"This type owns resources and cannot be implicitly copied",
                               "Consider using a reference ('&') to borrow the value instead",
                               NULL};
        tc_error_with_hints(tc, var_node->token, msg, hints);
    }
}

void mark_symbol_moved(ParserContext *ctx, ZenSymbol *sym, ASTNode *context_node)
{
    if (!sym)
    {
        return;
    }

    Type *t = sym->type_info;
    int copy = is_type_copy(ctx, t);

    if (t && ctx && !copy)
    {
        // sym->is_moved = 1; // DISABLE GLOBAL FLAG to rely on flow state
        // Keeping it for fallback if move_state is NULL?
        // Better to migrate fully, but for safety in mixed steps:
        if (!ctx->move_state)
        {
            sym->is_moved = 1;
        }

        if (ctx->move_state)
        {
            Token loc = context_node ? context_node->token : (Token){0};
            mark_moved_in_state(ctx->move_state, sym->name, loc);
        }
    }
}

void mark_valid_in_state(MoveState *state, const char *name, Token t)
{
    if (!state)
    {
        return;
    }

    MoveEntry *e = state->entries;
    while (e)
    {
        if (strcmp(e->symbol_name, name) == 0)
        {
            e->status = MOVE_STATE_VALID;
            e->moved_at = t;
            return;
        }
        e = e->next;
    }

    MoveEntry *new_entry = xmalloc(sizeof(MoveEntry));
    new_entry->symbol_name = xstrdup(name);
    new_entry->status = MOVE_STATE_VALID;
    new_entry->moved_at = t;
    new_entry->next = state->entries;
    state->entries = new_entry;
}

void mark_symbol_valid(ParserContext *ctx, ZenSymbol *sym, ASTNode *context_node)
{
    if (sym)
    {
        sym->is_moved = 0;
        if (ctx && ctx->move_state)
        {
            Token loc = context_node ? context_node->token : (Token){0};
            mark_valid_in_state(ctx->move_state, sym->name, loc);
        }
    }
}
