/*
 * Makes the RGB30's Mali-G52 shader binaries with Mesa's own Bifrost
 * compiler (MIT): a vertex shader that passes the position and a colour
 * through, and a fragment shader that writes the colour. Not built with the
 * firmware: it is linked against a Mesa tree built with
 * -Dgallium-drivers=panfrost (tools/mali/README.md), and its output,
 * src/rgb30/mali_shaders.h, is committed.
 *
 *   mkshader OUT.h
 */
#include <stdio.h>
#include <stdlib.h>

#include "nir.h"
#include "nir_builder.h"
#include "pan_compiler.h"
#include "util/u_dynarray.h"
#include "compiler/glsl_types.h"

#define GPU_ID 0x72120000u          /* Mali-G52 r1 (the RK3566's), arch 7 */

static int glsl_size(const struct glsl_type *t, bool bindless)
{
    (void)bindless;
    return glsl_count_attribute_slots(t, false);
}

static nir_variable *var(nir_builder *b, nir_variable_mode mode, const char *name, int loc, int interp)
{
    nir_variable *v = nir_variable_create(b->shader, mode, glsl_vec4_type(), name);
    v->data.location = loc;
    v->data.interpolation = interp;
    return v;
}

/* The vertices are in pixels: the viewport transform is the identity. nir_inline_sysval makes ONE component of the
 * constant, so a vec3 scale read through .y / .z gets folded to 0 (the y and z of every position vanish); a vec3 constant
 * does not. */
static bool is_viewport_scale(const nir_instr *instr, const void *data)
{
    (void)data;
    return instr->type == nir_instr_type_intrinsic &&
           nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_viewport_scale;
}

static nir_def *unit_scale(nir_builder *b, nir_instr *instr, void *data)
{
    (void)instr;
    (void)data;
    return nir_imm_vec3(b, 1.0f, 1.0f, 1.0f);
}

static void compile(const char *name, nir_shader *s, FILE *out, struct pan_shader_info *info_out, int full_scale)
{
    nir_validate_shader(s, "built");
    pan_preprocess_nir(s, GPU_ID);
    /* what the GL front ends do before lowering the I/O: every output store ends the shader */
    NIR_PASS(_, s, nir_lower_io_vars_to_temporaries, nir_shader_get_entrypoint(s), nir_var_shader_out);
    NIR_PASS(_, s, nir_split_var_copies);
    NIR_PASS(_, s, nir_lower_var_copies);
    NIR_PASS(_, s, nir_lower_global_vars_to_local);
    NIR_PASS(_, s, nir_lower_io, nir_var_shader_in | nir_var_shader_out, glsl_size,
             nir_lower_io_use_interpolated_input_intrinsics);
    NIR_PASS(_, s, nir_lower_vars_to_ssa);
    if (getenv("MKSHADER_NIR0"))
        nir_print_shader(s, stderr);
    pan_optimize_nir(s, GPU_ID);
    if (getenv("MKSHADER_NIR1"))
        nir_print_shader(s, stderr);
    pan_postprocess_nir(s, GPU_ID);

    if (getenv("MKSHADER_NIR2"))
        nir_print_shader(s, stderr);
    if (s->info.stage == MESA_SHADER_VERTEX) {
        NIR_PASS(_, s, nir_inline_sysval, nir_intrinsic_load_noperspective_varyings_pan, 0);
        /* the vertices are already in pixels: the shader's viewport transform is the identity */
        if (full_scale)
            NIR_PASS(_, s, nir_shader_lower_instructions, is_viewport_scale, unit_scale, NULL);
        else
            NIR_PASS(_, s, nir_inline_sysval, nir_intrinsic_load_viewport_scale, 0x3f800000u);
        NIR_PASS(_, s, nir_inline_sysval, nir_intrinsic_load_viewport_offset, 0);
        NIR_PASS(_, s, nir_opt_constant_folding);
        NIR_PASS(_, s, nir_opt_dce);
    }
    if (getenv("MKSHADER_NIR"))
        nir_print_shader(s, stderr);
    struct pan_compile_inputs inputs = { .gpu_id = GPU_ID, .no_idvs = true, .pushable_ubos = 1 };
    if (s->info.stage == MESA_SHADER_FRAGMENT)
        inputs.fixed_varying_mask = 0;
    struct util_dynarray bin;
    util_dynarray_init(&bin, NULL);
    struct pan_shader_info info = { 0 };
    pan_shader_compile(s, &inputs, &bin, &info);
    if (!bin.size) {
        fprintf(stderr, "%s: no binary\n", name);
        exit(1);
    }
    fprintf(stderr, "%s: %u bytes, %u work registers, preload %llx, tls %u\n", name, (unsigned)bin.size,
            info.work_reg_count, (unsigned long long)info.preload, info.tls_size);
    if (getenv("MKSHADER_DISASM"))
        pan_disassemble(stderr, bin.data, bin.size, GPU_ID, false);
    fprintf(stderr, "  push: %u words", info.push.count);
    for (unsigned i = 0; i < info.push.count; i++)
        fprintf(stderr, " (ubo %u + %u)", info.push.words[i].ubo, info.push.words[i].offset);
    fprintf(stderr, "; fau %u\n", info.fau_consts_count);
    if (s->info.stage == MESA_SHADER_VERTEX)
        fprintf(stderr, "  vs: idvs %d secondary %d offset %u regs %u preload %llx\n", info.vs.idvs,
                info.vs.secondary_enable, info.vs.secondary_offset, info.vs.secondary_work_reg_count,
                (unsigned long long)info.vs.secondary_preload);
    fprintf(out, "/* %s: %u work registers, preload 0x%llx */\n", name, info.work_reg_count,
            (unsigned long long)info.preload);
    fprintf(out, "#define %s_WORK_REGS %u\n#define %s_PRELOAD 0x%llxull\n", name, info.work_reg_count, name,
            (unsigned long long)info.preload);
    fprintf(out, "#define %s_UBO_COUNT %u\n#define %s_PUSH_COUNT %u\n", name, info.ubo_count, name, info.push.count);
    /* what the Renderer State needs (pan_shader_prepare_rsd): the Shader struct's counts, the blend type */
    fprintf(out, "#define %s_ATTRIBUTE_COUNT %u\n#define %s_VARYING_COUNT %u\n", name, info.attribute_count, name,
            info.varyings.input_count + info.varyings.output_count);
    fprintf(out, "#define %s_BLEND0_TYPE %u   /* nir_alu_type of colour output 0 */\n", name,
            (unsigned)info.bifrost.blend[0].type);
    fprintf(out, "#define %s_WAIT6 %d\n#define %s_WAIT7 %d   /* shader wait dependency 6 and 7 */\n", name,
            info.bifrost.wait_6, name, info.bifrost.wait_7);
    fprintf(out, "static const uint16_t %s_push[%u + 1][2] = {", name, info.push.count);
    for (unsigned i = 0; i < info.push.count; i++)
        fprintf(out, " { %u, %u },", info.push.words[i].ubo, info.push.words[i].offset);
    fprintf(out, " { 0, 0 } };   /* (ubo, byte offset) of each pushed word */\n");
    fprintf(out, "static const uint8_t %s_bin[%u] __attribute__((aligned(128))) = {", name, (unsigned)bin.size);
    for (unsigned i = 0; i < bin.size; i++)
        fprintf(out, "%s0x%02x,", i % 16 ? " " : "\n    ", ((uint8_t *)bin.data)[i]);
    fprintf(out, "\n};\n\n");
    *info_out = info;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: mkshader OUT.h\n");
        return 1;
    }
    /* no message preload (the textured fragment shader would preload its varying): it needs the Renderer State's
     * message_preload fields, a plain LD_VAR does not. Read once, at the first compile. */
    setenv("BIFROST_MESA_DEBUG", "nopreload", 1);
    FILE *out = fopen(argv[1], "w");
    if (!out)
        return 1;
    fprintf(out, "/* made by tools/mali/mkshader.c with Mesa's Bifrost compiler (MIT): do not edit */\n"
                 "#include <stdint.h>\n\n");

    const nir_shader_compiler_options *opt = pan_get_nir_shader_compiler_options(7);
    struct pan_shader_info vi, fi;

    {   /* vertex: gl_Position = pos */
        nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, opt, "bm_vs");
        b.shader->info.first_ubo_is_default_ubo = true;
        nir_variable *ipos = var(&b, nir_var_shader_in, "pos", VERT_ATTRIB_GENERIC0, INTERP_MODE_NONE);
        nir_variable *opos = var(&b, nir_var_shader_out, "gl_Position", VARYING_SLOT_POS, INTERP_MODE_NONE);
        nir_store_var(&b, opos, nir_load_var(&b, ipos), 0xf);
        nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
        compile("BM_VS", b.shader, out, &vi, 1);
    }
    {   /* vertex with the transform: gl_Position = M * pos, M a row-major 4x4 matrix in uniform buffer 0 (16 pushed words) */
        nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, opt, "bm_vs3");
        b.shader->info.first_ubo_is_default_ubo = true;
        b.shader->info.num_ubos = 1;
        nir_variable *ipos = var(&b, nir_var_shader_in, "pos", VERT_ATTRIB_GENERIC0, INTERP_MODE_NONE);
        nir_variable *opos = var(&b, nir_var_shader_out, "gl_Position", VARYING_SLOT_POS, INTERP_MODE_NONE);
        nir_def *p = nir_load_var(&b, ipos);
        nir_def *r[4];
        for (int i = 0; i < 4; i++) {
            nir_def *row = nir_load_ubo(&b, 4, 32, nir_imm_int(&b, 0), nir_imm_int(&b, 16 * i), .align_mul = 16,
                                        .align_offset = 0, .range_base = 0, .range = 64);
            r[i] = nir_fdot4(&b, row, p);
        }
        nir_store_var(&b, opos, nir_vec4(&b, r[0], r[1], r[2], r[3]), 0xf);
        nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
        compile("BM_VS3", b.shader, out, &vi, 1);
    }
    {   /* fragment: colour 0 = the draw's colour, a vec4 at offset 0 of uniform buffer 0 (pushed) */
        nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, opt, "bm_fs");
        b.shader->info.first_ubo_is_default_ubo = true;
        b.shader->info.num_ubos = 1;
        nir_variable *ocol = var(&b, nir_var_shader_out, "color", FRAG_RESULT_DATA0, INTERP_MODE_NONE);
        nir_def *c = nir_load_ubo(&b, 4, 32, nir_imm_int(&b, 0), nir_imm_int(&b, 0), .align_mul = 16,
                                  .align_offset = 0, .range_base = 0, .range = 16);
        nir_store_var(&b, ocol, c, 0xf);
        nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
        compile("BM_FS", b.shader, out, &fi, 0);
    }
    {   /* textured vertex: position (attribute 0) and (u, v, k, 0) (attribute 1, the same buffer 16 bytes on) to varying 0 */
        nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, opt, "bm_vs_tex");
        b.shader->info.first_ubo_is_default_ubo = true;
        nir_variable *ipos = var(&b, nir_var_shader_in, "pos", VERT_ATTRIB_GENERIC0, INTERP_MODE_NONE);
        nir_variable *iuv = var(&b, nir_var_shader_in, "uvk", VERT_ATTRIB_GENERIC1, INTERP_MODE_NONE);
        iuv->data.driver_location = 1;      /* the attribute's index: lowered I/O takes it as `base` */
        nir_variable *opos = var(&b, nir_var_shader_out, "gl_Position", VARYING_SLOT_POS, INTERP_MODE_NONE);
        nir_variable *ouv = var(&b, nir_var_shader_out, "uvk_out", VARYING_SLOT_VAR0, INTERP_MODE_SMOOTH);
        ouv->data.driver_location = 1;
        nir_store_var(&b, opos, nir_load_var(&b, ipos), 0xf);
        nir_store_var(&b, ouv, nir_load_var(&b, iuv), 0xf);
        nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
        compile("BM_VS_TEX", b.shader, out, &vi, 1);
    }
    {   /* textured fragment: colour 0 = texel (texture 0, sampler 0, normalised coordinates, level 0) * k, alpha 1 */
        nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, opt, "bm_fs_tex");
        b.shader->info.first_ubo_is_default_ubo = true;
        nir_variable *iuv = var(&b, nir_var_shader_in, "uvk", VARYING_SLOT_VAR0, INTERP_MODE_SMOOTH);
        nir_variable *ocol = var(&b, nir_var_shader_out, "color", FRAG_RESULT_DATA0, INTERP_MODE_NONE);
        nir_def *uvk = nir_load_var(&b, iuv);
        nir_def *uv = nir_trim_vector(&b, uvk, 2);
        nir_def *k = nir_channel(&b, uvk, 2);
        nir_def *texel = nir_build_tex(&b, nir_texop_txl, .coord = uv, .lod = nir_imm_float(&b, 0.0f),
                                       .texture_index = 0, .sampler_index = 0, .dim = GLSL_SAMPLER_DIM_2D,
                                       .dest_type = nir_type_float32);
        nir_def *rgb = nir_fmul(&b, nir_trim_vector(&b, texel, 3), k);
        /* blue at least 1/255: a pixel of the key colour 0xFF00FF means "not covered" to the resolve, and blue 0 and 1
         * are the same 5 bits of the RGB565 page */
        nir_def *bl = nir_fmax(&b, nir_channel(&b, rgb, 2), nir_imm_float(&b, 1.0f / 255.0f));
        nir_store_var(&b, ocol, nir_vec4(&b, nir_channel(&b, rgb, 0), nir_channel(&b, rgb, 1), bl,
                                         nir_imm_float(&b, 1.0f)), 0xf);
        b.shader->info.num_textures = 1;
        nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
        compile("BM_FS_TEX", b.shader, out, &fi, 0);
    }
    fclose(out);
    return 0;
}
