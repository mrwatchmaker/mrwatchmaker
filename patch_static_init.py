import sys
import re

output_panel_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\output_panel.c"
i18n_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\i18n.c"

# 1. Update output_panel.c
with open(output_panel_path, 'r', encoding='utf-8') as f:
    op_code = f.read()

# Fix static const char *fixed_names initialization
op_code = op_code.replace(
    'static const char *fixed_names[4] = {_("🕘 9시 측정"), _("🕛 12시 측정"), _("🕒 3시 측정"), _("🕕 6시 측정")};',
    'static const char *fixed_names[4] = {"🕘 9시 측정", "🕛 12시 측정", "🕒 3시 측정", "🕕 6시 측정"};'
)
op_code = op_code.replace(
    'gtk_button_set_label(GTK_BUTTON(op->manual_measure_buttons[i]), fixed_names[i]);',
    'gtk_button_set_label(GTK_BUTTON(op->manual_measure_buttons[i]), _(fixed_names[i]));'
)

# Fix "★%d 측정"
op_code = op_code.replace(
    'snprintf(buf, sizeof(buf), "★%d 측정", c + 1);',
    'snprintf(buf, sizeof(buf), _("★%d 측정"), c + 1);'
)

# Wait, there's another countdown string
# snprintf(buf, sizeof(buf), "측정 중... %d 초", op->manual_measure_countdown);
op_code = op_code.replace(
    'snprintf(buf, sizeof(buf), "측정 중... %d 초", op->manual_measure_countdown);',
    'snprintf(buf, sizeof(buf), _("측정 중... %d 초"), op->manual_measure_countdown);'
)

with open(output_panel_path, 'w', encoding='utf-8') as f:
    f.write(op_code)

# 2. Update i18n.c
strings_to_add = [
    ("★%d 측정", "★Measure %d", "★%d 測定", "★测量 %d", "★Mesurer %d"),
    ("측정 중... %d 초", "Measuring... %d s", "測定中... %d 秒", "测量中... %d 秒", "Mesure... %d s")
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

print("Fixed output_panel.c static initializer and added missing strings.")
