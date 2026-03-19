import sys
import re

i18n_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\i18n.c"

with open(i18n_path, 'r', encoding='utf-8') as f:
    text = f.read()

# Fix the translations for the multiline strings where we used double slashes or messed up newlines.
# If they are exactly what is in output_panel.c, they need to match character by character.
# In output_panel.c, we have:
# _("  평균 레이트가 우수한 범위에 있습니다.\n  시계의 전반적인 조율 상태가 양호합니다.\n")

# Wait, in patch_multiline2.py, I escaped newlines as \\n which makes the literal C string have two characters \ and n, not a real newline.
# Let's clean up the dictionaries.
# I will use a regex to find all dict_entry_t blocks and replace \\n with \n inside the first element of the pair.

text = text.replace(r'\\n', r'\n')
text = text.replace(r'\"', r'"')

with open(i18n_path, 'w', encoding='utf-8') as f:
    f.write(text)

print("Fixed double escaped characters in i18n.c")
