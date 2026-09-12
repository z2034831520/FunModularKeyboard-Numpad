#ifndef UI_MAINSCREEN_H
#define UI_MAINSCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

void ui_MainScreen_screen_init(void);
void ui_MainScreen_screen_destroy(void);
void ui_MainScreen_set_work_mode(const char *mode);

extern lv_obj_t * ui_MainScreen;
extern lv_obj_t * ui_LabelTime;
extern lv_obj_t * ui_LabelData;
extern lv_obj_t * ui_LabelSecond;
extern lv_obj_t * ui_LabelWeek;
extern lv_obj_t * ui_LabelWorkmode;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
