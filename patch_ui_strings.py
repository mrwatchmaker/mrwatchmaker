import sys
import re

output_panel_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\output_panel.c"
i18n_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\i18n.c"

# 1. Update output_panel.c
with open(output_panel_path, 'r', encoding='utf-8') as f:
    op_code = f.read()

# Replace pos_names
op_code = op_code.replace(
    'const char* pos_names[] = {"🕘 9시 (기본)", "🕛 12시", "🕒 3시", "🕕 6시"};',
    'const char* pos_names[] = {_("🕘 9시 (기본)"), _("🕛 12시"), _("🕒 3시"), _("🕕 6시")};'
)

# Replace fixed_names
op_code = op_code.replace(
    'static const char *fixed_names[4] = {"🕘 9시 측정", "🕛 12시 측정", "🕒 3시 측정", "🕕 6시 측정"};',
    'static const char *fixed_names[4] = {_("🕘 9시 측정"), _("🕛 12시 측정"), _("🕒 3시 측정"), _("🕕 6시 측정")};'
)

# Replace snprintf for 커스텀 n 측정
op_code = op_code.replace(
    'snprintf(buf, sizeof(buf), "커스텀 %d 측정", c + 1);',
    'snprintf(buf, sizeof(buf), _("커스텀 %d 측정"), c + 1);'
)

# Replace snprintf for 커스텀 n
op_code = op_code.replace(
    'snprintf(buf, sizeof(buf), "커스텀 %d", slot_idx - 3);',
    'snprintf(buf, sizeof(buf), _("커스텀 %d"), slot_idx - 3);'
)
op_code = op_code.replace(
    'snprintf(buf, sizeof(buf), "커스텀 %d", slot_idx + 1);',
    'snprintf(buf, sizeof(buf), _("커스텀 %d"), slot_idx + 1);'
)

# Fix winder_presets usage in gtk_combo_box_text_append_text
op_code = op_code.replace(
    'gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(op->winder_preset_combo),\n			                               winder_presets[i].name);',
    'gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(op->winder_preset_combo),\n			                               _(winder_presets[i].name));'
)
op_code = op_code.replace(
    'gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(op->winder_preset_combo), winder_presets[i].name);',
    'gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(op->winder_preset_combo), _(winder_presets[i].name));'
)


# Fix winder_pos_name usage
op_code = op_code.replace(
    'if (slot_idx < 4) return winder_pos_name[slot_idx];',
    'if (slot_idx < 4) return _(winder_pos_name[slot_idx]);'
)

op_code = op_code.replace(
    'cur_name = winder_pos_name[cur_pos];',
    'cur_name = _(winder_pos_name[cur_pos]);'
)
op_code = op_code.replace(
    'next_name = winder_pos_name[next_pos];',
    'next_name = _(winder_pos_name[next_pos]);'
)

# Write back
with open(output_panel_path, 'w', encoding='utf-8') as f:
    f.write(op_code)

# 2. Update i18n.c
strings_to_add = [
    ("🕘 9시 (기본)", "🕘 9 o'clock (Base)", "🕘 9時 (基本)", "🕘 9点 (基础)", "🕘 9 heures (Base)"),
    ("🕛 12시", "🕛 12 o'clock", "🕛 12時", "🕛 12点", "🕛 12 heures"),
    ("🕒 3시", "🕒 3 o'clock", "🕒 3時", "🕒 3点", "🕒 3 heures"),
    ("🕕 6시", "🕕 6 o'clock", "🕕 6時", "🕕 6点", "🕕 6 heures"),
    
    ("★ 커스텀 %d  (%d, %d)", "★ Custom %d  (%d, %d)", "★ カスタム %d  (%d, %d)", "★ 自定义 %d  (%d, %d)", "★ Personnalisé %d  (%d, %d)"),
    
    ("🕘 9시 측정", "🕘 Measure 9 o'clock", "🕘 9時測定", "🕘 测量 9点", "🕘 Mesurer 9 h"),
    ("🕛 12시 측정", "🕛 Measure 12 o'clock", "🕛 12時測定", "🕛 测量 12点", "🕛 Mesurer 12 h"),
    ("🕒 3시 측정", "🕒 Measure 3 o'clock", "🕒 3時測定", "🕒 测量 3点", "🕒 Mesurer 3 h"),
    ("🕕 6시 측정", "🕕 Measure 6 o'clock", "🕕 6時測定", "🕕 测量 6点", "🕕 Mesurer 6 h"),
    ("커스텀 %d 측정", "Measure Custom %d", "カスタム %d 測定", "测量自定义 %d", "Mesurer Perso %d"),
    
    ("커스텀 %d", "Custom %d", "カスタム %d", "自定义 %d", "Personnalisé %d"),
    ("9시", "9 o'clock", "9時", "9点", "9 heures"),
    
    ("풀 코스 (6→9→12→3→6→3→12→9)", "Full Course (6→9→12→3→6→3→12→9)", "フルコース (6→9→12→3→6→3→12→9)", "全方位 (6→9→12→3→6→3→12→9)", "Cours complet (6→9→12→3→6→3→12→9)"),
    ("시계방향 회전 (9→12→3→6)", "Clockwise (9→12→3→6)", "時計回り (9→12→3→6)", "顺时针 (9→12→3→6)", "Sens Horaire (9→12→3→6)"),
    ("반시계방향 회전 (9→6→3→12)", "Counter-Clockwise (9→6→3→12)", "反時計回り (9→6→3→12)", "逆时针 (9→6→3→12)", "Sens Anti-Horaire (9→6→3→12)"),
    ("좌우 스윙 (9시 ↔ 3시)", "L/R Swing (9 ↔ 3)", "左右スイング (9時 ↔ 3時)", "左右摇摆 (9点 ↔ 3点)", "Balancement G/D (9 ↔ 3)"),
    ("상하 스윙 (12시 ↔ 6시)", "U/D Swing (12 ↔ 6)", "上下スイング (12時 ↔ 6時)", "上下摇摆 (12点 ↔ 6点)", "Balancement H/B (12 ↔ 6)"),
    ("가볍게 흔들기 (9시 ↔ 6시)", "Light Shake (9 ↔ 6)", "軽く振る (9時 ↔ 6時)", "轻微摇晃 (9点 ↔ 6点)", "Secousse Légère (9 ↔ 6)"),
    ("6→9→12→9 왕복", "6→9→12→9 Round Trip", "6→9→12→9 往復", "6→9→12→9 往返", "6→9→12→9 Aller-Retour"),
    ("시계방향 왕복 (9→12→3→6→3→12→9)", "CW Round Trip (9→12→3→6→3→12→9)", "時計回り往復 (9→12→3→6→3→12→9)", "顺时针往返 (9→12→3→6→3→12→9)", "Aller-Retour Horaire (9→12→3→6→3→12→9)"),
    ("자세차 자동측정 순서 (반복)", "Auto Positional Order (Repeat)", "姿勢差自動測定順序 (繰り返し)", "方位自动测量顺序 (重复)", "Ordre Positionnel Auto (Répéter)"),
    ("커스텀 1~N 반복", "Custom 1~N Repeat", "カスタム 1~N 繰り返し", "自定义 1~N 重复", "Perso 1~N Répéter")
]

with open(i18n_path, 'r', encoding='utf-8') as f:
    i18n_code = f.read()

def insert_dict(content, dict_name, idx):
    match = re.search(r"static dict_entry_t " + dict_name + r"\[\] = \{([\s\S]*?)\{NULL, NULL\}", content)
    if not match:
        return content
    
    dict_content = match.group(1)
    for s in strings_to_add:
        if f'"{s[0]}"' in dict_content:
            continue
        key_str = s[0].replace('"', '\\"').replace('\n', '\\n')
        val_str = s[idx].replace('"', '\\"').replace('\n', '\\n')
        dict_content += f'    {{"{key_str}", "{val_str}"}},\n'
    
    new_block = f"static dict_entry_t {dict_name}[] = {{{dict_content}{{NULL, NULL}}"
    return content[:match.start()] + new_block + content[match.end():]

i18n_code = insert_dict(i18n_code, "dict_en", 1)
i18n_code = insert_dict(i18n_code, "dict_ja", 2)
i18n_code = insert_dict(i18n_code, "dict_zh", 3)
i18n_code = insert_dict(i18n_code, "dict_fr", 4)

# For ko
match = re.search(r"static dict_entry_t dict_ko\[\] = \{([\s\S]*?)\{NULL, NULL\}", i18n_code)
if match:
    dict_content = match.group(1)
    for s in strings_to_add:
        if f'"{s[0]}"' in dict_content:
            continue
        key_str = s[0].replace('"', '\\"').replace('\n', '\\n')
        dict_content += f'    {{"{key_str}", "{key_str}"}},\n'
    new_block = f"static dict_entry_t dict_ko[] = {{{dict_content}{{NULL, NULL}}"
    i18n_code = i18n_code[:match.start()] + new_block + i18n_code[match.end():]

with open(i18n_path, 'w', encoding='utf-8') as f:
    f.write(i18n_code)

print("Updated output_panel.c and i18n.c")
