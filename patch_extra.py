import sys
import re

output_panel_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\output_panel.c"
i18n_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\i18n.c"

strings_to_wrap = [
    r'"암이 걸렸습니다. 나사 풀지 말고 손으로 살짝 돌려 맞춘 뒤 [Base (9시)]를 다시 누르세요."',
    r'"암 토크 해제"',
    r'"커스텀 위치를 먼저 추가한 뒤 시작하세요."'
]

strings_to_add = [
    ("암이 걸렸습니다. 나사 풀지 말고 손으로 살짝 돌려 맞춘 뒤 [Base (9시)]를 다시 누르세요.", "Arm is stuck. Do not loosen screws; gently adjust by hand, then press [Base (9 o'clock)] again.", "アームが引っかかりました。ネジを緩めず手で軽く合わせてから、[Base (9時)]を再度押してください。", "臂被卡住了。请不要松开螺丝；用手轻轻调整，然后再次按下[Base (9点)]。", "Le bras est coincé. Ne desserrez pas les vis ; ajustez doucement à la main, puis appuyez de nouveau sur [Base (9h)]."),
    ("암 토크 해제", "Arm Torque Released", "アームトルク解除", "释放臂扭矩", "Couple du Bras Relâché"),
    ("커스텀 위치를 먼저 추가한 뒤 시작하세요.", "Please add a custom position before starting.", "先にカスタム位置を追加してから開始してください。", "请先添加自定义位置，然后再开始。", "Veuillez ajouter une position personnalisée avant de commencer.")
]

# 1. Update output_panel.c
with open(output_panel_path, 'r', encoding='utf-8') as f:
    op_code = f.read()

count = 0
for s in strings_to_wrap:
    if f"_{s}" not in op_code and f"_({s})" not in op_code:
        if s in op_code:
            op_code = op_code.replace(s, f"_({s})")
            count += 1
        else:
            pass # suppress print to avoid UnicodeEncodeError

with open(output_panel_path, 'w', encoding='utf-8') as f:
    f.write(op_code)

print(f"Replaced {count} strings in output_panel.c")

# 2. Update i18n.c
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

print("Updated output_panel.c and i18n.c with additional strings.")
