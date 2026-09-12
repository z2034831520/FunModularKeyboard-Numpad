// Reduced SquareLine helper set for the widgets used by this project.

#include "ui_helpers.h"

void _ui_screen_change(lv_obj_t **target,
                       lv_scr_load_anim_t animation,
                       int speed,
                       int delay,
                       void (*target_init)(void))
{
    if (*target == NULL)
    {
        target_init();
    }
    lv_scr_load_anim(*target, animation, speed, delay, false);
}
