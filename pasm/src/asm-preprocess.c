#include "asm-preprocess.h"

bool made_for_preprocessing_stage[] = {
    false,            // NODE_KIND_INSTRUCTION
    true,             // NODE_KIND_LABEL_DEF
    true,             // NODE_KIND_CONST_DEF
    false,            // NODE_KIND_ENTRY
    true,             // NODE_KIND_USE
    true,             // NODE_KIND_MACRO
    false,            // NODE_KIND_MACRO_CALL
};

void pasm_process_const(PASM *self, PASM_Const constant) {
    char *name = cstr_from_sv(constant.name);
    PASM_Arg value = constant.value; 
    pasm_process_const_parts(self, name, value);  
}

void pasm_process_consts_node(PASM *self, PASM_Node node) {
    for (size_t i = 0; i < node.as.constants.count; ++i) {
        pasm_process_const(self, node.as.constants.items[i]);
    }
}

void pasm_process_label_node(PASM *self, PASM_Node node) {
    char *label_name = cstr_from_sv(node.as.label);
    pasm_process_label_parts(self, label_name, self->prog_size);
}

void pasm_add_to_global_context(PASM *self, char *name, PASM_Context_Value value) {
    // the call below will throw an error if the name of already exists in the main context
    PASM_Context global_context = self->contexts.items[0];
    
    if (pasm_has_this_identifier_in_this_context(global_context, name)) {
        THROW_ERROR("`%s` already declared", name);
    }

    // add it if no error occured
    pasm_add_context_value_to_context(&global_context, name, value);
    self->contexts.items[0] = global_context;
}

void pasm_add_context(PASM *self, PASM_Context context) {
    for (size_t i = 0; i < HASH_MAP_SIZE; ++i) {
        HashMap_Node *node = context.map.nodes[i];
        if (!node) { continue; }
        while (node) {
            PASM_Context_Value value = *(PASM_Context_Value *)node->value;
            pasm_add_to_global_context(self, node->key, value);
            node = node->next;
        }
    }
}

void pasm_add_program(PASM *self, PASM_Prog prog) {
    if (prog.count == 0) { return; }
    if (self->prog.size == 0) { DA_INIT(&self->prog, sizeof(Program_Inst)); }
    
    for (size_t i = 0; i < prog.count; ++i) {
        Program_Inst prog_inst = prog.items[i];
        pasm_process_instruction_parts(self, prog_inst.as.inst.kind, prog_inst.as.inst.ops);
        DA_APPEND(&self->prog, prog_inst);
    }

    self->prog_size = self->prog.count;
}

void pasm_process_external_file(PASM *self, PASM_Node node) {
    // get the file name
    char *filename = cstr_from_sv(node.as.file_path);


    // getting the absolute path of the current file
    char *root_file_abs_path = calloc(FILENAME_MAX + 1, sizeof(char));
    realpath(self->filename, root_file_abs_path);


    // getting the abs path the sub file 
    char *sub_file_abs_path = calloc(FILENAME_MAX + 1, sizeof(char));
    if (!realpath(filename, sub_file_abs_path)) {
        THROW_ERROR("file `%s` included in %s not found", filename, self->filename);
    }

    // check if the file is already used
    if (pasm_check_file_included(self, sub_file_abs_path)) {
        THROW_ERROR("file `%s` used twice in the current file `%s`", filename, self->filename);
    }

    // check for circular uses
    if (pasm_has_super_file(self, sub_file_abs_path)) {
        THROW_ERROR("circular dependency found between `%s` and `%s`", self->filename, filename);
    }

    PASM pasm = pasm_init(filename);

    // add it to the super files
    pasm_add_super_file(&pasm, root_file_abs_path);
    pasm_add_super_files(&pasm, self->sup_files);

    // add it to the sub files
    pasm_add_sub_file(self, sub_file_abs_path);

    compile(&pasm);

    // add the context
    pasm_add_context(self, pasm.contexts.items[0]);

    // add the prog
    pasm_add_program(self, pasm.prog);

    free(filename);
}

void pasm_process_macro_def_parts(PASM *self, char *name, PASM_Macro_Arg_Names names, PASM_Nodes block) {
    int64_t index = pasm_has_this_identifier_in_contexts(self, name);
    
    if (index != -1) {
        PASM_Context context = self->contexts.items[index];
        PASM_Context_Value context_value = pasm_get_context_value_of_identifier(context, name);
        
        if (context_value.type == PASM_CONTEXT_VALUE_TYPE_LABEL) {
            THROW_ERROR("`%s` first defined as a label", name);
        }
        if (context_value.type == PASM_CONTEXT_VALUE_TYPE_CONST) {
            THROW_ERROR("`%s` first defined as a constant", name);
        }
        if (context_value.type == PASM_CONTEXT_VALUE_TYPE_MACRO) {
            THROW_ERROR("`%s` defined twice as a macro", name);
        }
        ASSERT(false, "unreachable");
    }

    PASM_Context_Macro macro = {0};
    macro.args = names;
    macro.block = block;
    macro.name = name;

    // add the macro defintion to the block
    PASM_Context_Value context_value = { .type = PASM_CONTEXT_VALUE_TYPE_MACRO, .as.macro = macro };
    
    // get the global context and append the macro to it
    pasm_add_context_value_to_context(&self->contexts.items[0], name, context_value);
}

void pasm_process_macro_node(PASM *self, PASM_Node node) {
    char *macro_name = cstr_from_sv(node.as.macro_def.name);
    pasm_process_macro_def_parts(self, macro_name, node.as.macro_def.arg_names, *node.as.macro_def.block);
}

void pasm_preprocess_node(PASM *self, PASM_Nodes nodes, size_t current) {
    if (nodes.items[current].kind == NODE_KIND_CONST_DEF) { 
        pasm_process_consts_node(self, nodes.items[current]);
        return;
    }

    if (nodes.items[current].kind == NODE_KIND_LABEL_DEF) {
        pasm_process_label_node(self, nodes.items[current]);
        return;
    }

    if (nodes.items[current].kind == NODE_KIND_MACRO) {
        pasm_process_macro_node(self, nodes.items[current]);
        return;
    }

    if (nodes.items[current].kind == NODE_KIND_USE) {
        pasm_process_external_file(self, nodes.items[current]);
        return;
    }

    ASSERT(false, "unreachable");
}

void pasm_preprocess(PASM *self, PASM_Nodes nodes) {
    for (size_t i = 0; i < nodes.count; ++i) {
        
        if (made_for_preprocessing_stage[nodes.items[i].kind]) {
            pasm_preprocess_node(self, nodes, i);
            continue;
        } 
        
        if (nodes.items[i].kind == NODE_KIND_INSTRUCTION || nodes.items[i].kind == NODE_KIND_MACRO_CALL) { self->prog_size++; }
    }
}
