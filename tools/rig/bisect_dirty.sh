#!/bin/bash
S="$(cd "$(dirname "$0")" && pwd)"
A="line_smooth point_smooth line_stipple line_stipple3 polygon_offset polymode_line polymode_point cull scissor clip_plane logic"
B="tex_npot tex_rect tex_cube wrap_mirror wrap_mirror_clamp wrap_edge wrap_border wrap_repeat tex1x1 envstate cpu_persp env_add combine_addsigned combine_subtract combine_dot3"
C="tex_s3tc tex_paletted tex_aniso tex_3d copyteximage copytexsub point_sprite point_params displaylist vertex_array lighting fog_coord secondary_color"
D="stencil stencil_twoside occlusion vertex_program fragment_program depth_clamp accum accum2 fence flush_render vao multidraw range_elements genmip windowpos"
n=70
for g in A B C D; do
  eval "names=\$$g"
  n=$((n+1)); bash $S/redeploy.sh $n >/dev/null 2>&1
  ssh MDD "sh ~/rungui.sh /tmp/g.out 200 \"~/rage-p1/tools/ragegl/gl_probe $names\"" >/dev/null 2>&1
  r=$(ssh MDD 'sh ~/rungui.sh /tmp/c.out 100 "~/rage-p1/tools/ragegl/gl_probe blend_sep" | grep -E "PASS|FAIL"')
  echo "group $g: $r"
done
