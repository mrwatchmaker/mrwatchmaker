import sys

i18n_path = r"c:\Users\USER\Desktop\watch_time\tg-timer-0.5.0\src\i18n.c"

with open(i18n_path, 'r', encoding='utf-8') as f:
    text = f.read()

# Fix the escape characters
text = text.replace(r'\\"F(Fast)\\"', r'\"F(Fast)\"')
text = text.replace(r'\\"S(Slow)\\"', r'\"S(Slow)\"')

with open(i18n_path, 'w', encoding='utf-8') as f:
    f.write(text)

print("Fixed i18n.c escape sequences.")
