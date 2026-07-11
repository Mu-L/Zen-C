// SPDX-License-Identifier: MIT
#include "../parser/parser.h"

#include "codegen.h"
#include "zprep.h"
#include "../constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../plugins/plugin_manager.h"
#include "ast.h"
#include "zprep_plugin.h"
#include "codegen_internal.h"

void handle_block(ParserContext *ctx, ASTNode *node)
{
    int saved = ctx->cg.defer_count;
    EMIT(ctx, "({ ");
    codegen_walker(ctx, node->block.statements);
    for (int i = ctx->cg.defer_count - 1; i >= saved; i--)
    {
        emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
        codegen_node_single(ctx, ctx->cg.defer_stack[i]);
    }
    ctx->cg.defer_count = saved;
    EMIT(ctx, " })");
}

void handle_if_expr(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "({ ");

    ASTNode *then_result = NULL;
    if (node->if_stmt.then_body && node->if_stmt.then_body->type == NODE_BLOCK)
    {
        ASTNode *stmt = node->if_stmt.then_body->block.statements;
        while (stmt && stmt->next)
        {
            stmt = stmt->next;
        }
        then_result = stmt;
    }
    else
    {
        then_result = node->if_stmt.then_body;
    }

    if (then_result)
    {
        EMIT(ctx, "__typeof__(");
        codegen_expression(ctx, then_result);
        EMIT(ctx, ") _ifval; ");
    }
    else
    {
        EMIT(ctx, "int _ifval; ");
    }

    EMIT(ctx, "if (");
    codegen_expression(ctx, node->if_stmt.condition);
    EMIT(ctx, ") { ");
    if (node->if_stmt.then_body && node->if_stmt.then_body->type == NODE_BLOCK)
    {
        ASTNode *stmt = node->if_stmt.then_body->block.statements;
        while (stmt && stmt->next)
        {
            codegen_node_single(ctx, stmt);
            stmt = stmt->next;
        }
        if (stmt)
        {
            EMIT(ctx, "_ifval = ");
            codegen_expression(ctx, stmt);
            EMIT(ctx, "; ");
        }
    }
    else if (node->if_stmt.then_body)
    {
        EMIT(ctx, "_ifval = ");
        codegen_expression(ctx, node->if_stmt.then_body);
        EMIT(ctx, "; ");
    }
    EMIT(ctx, "} else { ");
    if (node->if_stmt.else_body && node->if_stmt.else_body->type == NODE_BLOCK)
    {
        ASTNode *stmt = node->if_stmt.else_body->block.statements;
        while (stmt && stmt->next)
        {
            codegen_node_single(ctx, stmt);
            stmt = stmt->next;
        }
        if (stmt)
        {
            EMIT(ctx, "_ifval = ");
            codegen_expression(ctx, stmt);
            EMIT(ctx, "; ");
        }
    }
    else if (node->if_stmt.else_body)
    {
        EMIT(ctx, "_ifval = ");
        codegen_expression(ctx, node->if_stmt.else_body);
        EMIT(ctx, "; ");
    }
    EMIT(ctx, "} _ifval; })");
}

void handle_try_expr(ParserContext *ctx, ASTNode *node)
{
    char *type_name = "Result";
    Type *expr_type = node->try_stmt.expr->type_info;

    if (expr_type && expr_type->name)
    {
        type_name = expr_type->name;
    }
    else if (ctx->cg.current_func_ret_type)
    {
        type_name = ctx->cg.current_func_ret_type;
    }

    if (strcmp(type_name, "__auto_type") == 0 || strcmp(type_name, "unknown") == 0)
    {
        type_name = "Result";
    }

    char *search_name = type_name;
    if (strncmp(search_name, "struct ", 7) == 0)
    {
        search_name += 7;
    }

    int is_enum = 0;
    StructRef *er = ctx->parsed_enums_list;
    while (er)
    {
        if (er->node && er->node->type == NODE_ENUM && strcmp(er->node->enm.name, search_name) == 0)
        {
            is_enum = 1;
            break;
        }
        er = er->next;
    }
    if (!is_enum)
    {
        ASTNode *ins = ctx->instantiated_structs;
        while (ins)
        {
            if (ins->type == NODE_ENUM && strcmp(ins->enm.name, search_name) == 0)
            {
                is_enum = 1;
                break;
            }
            ins = ins->next;
        }
    }

    int is_option = str_is_option_type(search_name);

    EMIT(ctx, "({ ");
    emit_auto_type(ctx, node->try_stmt.expr, node->token);
    EMIT(ctx, " _try = ");
    codegen_expression(ctx, node->try_stmt.expr);

    if (is_option)
    {
        if (is_enum)
        {
            EMIT(ctx,
                 "; if (_try.tag == %s__None_Tag) return (%s__None()); _try.data.Some; })",
                 search_name, search_name);
        }
        else
        {
            EMIT(ctx,
                 "; if (!_try.is_some) return %s__None(); _try.val; })",
                 search_name);
        }
    }
    else if (is_enum)
    {
        EMIT(ctx,
             "; if (_try.tag == %s__Err_Tag) return (%s__Err(_try.data.Err)); _try.data.Ok; })",
             search_name, search_name);
    }
    else
    {
        EMIT(ctx, "; if (!_try.is_ok) return %s__Err(_try.err); _try.val; })", search_name);
    }
}

void handle_plugin(ParserContext *ctx, ASTNode *node)
{
    ZPlugin *found = ctx->hook_find_plugin
                         ? (ZPlugin *)ctx->hook_find_plugin(node->plugin_stmt.plugin_name)
                         : NULL;
    if (found)
    {
        ZApi api;
        if (ctx->hook_plugin_init_api)
        {
            ctx->hook_plugin_init_api(&api, ctx->current_filename, node->line, ctx->config);
        }
        api.out = ctx->cg.emitter.file;
        api.hoist_out = ctx->cg.hoist_out;
        found->fn(node->plugin_stmt.body, &api);
    }
    else
    {
        EMIT(ctx, "/* Unknown plugin: %s */\n", node->plugin_stmt.plugin_name);
    }
}
