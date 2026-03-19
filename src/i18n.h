#ifndef I18N_H
#define I18N_H

void i18n_init(int lang_idx);
const char *_(const char *key);
int i18n_get_current_lang(void);
int i18n_get_num_langs(void);
const char *i18n_get_lang_name(int idx);

#endif
