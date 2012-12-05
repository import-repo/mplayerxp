#ifndef JOYSTICK_H_INCLUDED
#define JOYSTICK_H_INCLUDED 1

namespace mpxp {
    enum {
	JOY_BASE	=(0x100+128),
	JOY_AXIS0_PLUS	=(JOY_BASE+0),
	JOY_AXIS0_MINUS	=(JOY_BASE+1),
	JOY_AXIS1_PLUS	=(JOY_BASE+2),
	JOY_AXIS1_MINUS	=(JOY_BASE+3),
	JOY_AXIS2_PLUS	=(JOY_BASE+4),
	JOY_AXIS2_MINUS	=(JOY_BASE+5),
	JOY_AXIS3_PLUS	=(JOY_BASE+6),
	JOY_AXIS3_MINUS	=(JOY_BASE+7),
	JOY_AXIS4_PLUS	=(JOY_BASE+8),
	JOY_AXIS4_MINUS	=(JOY_BASE+9),
	JOY_AXIS5_PLUS	=(JOY_BASE+10),
	JOY_AXIS5_MINUS	=(JOY_BASE+11),
	JOY_AXIS6_PLUS	=(JOY_BASE+12),
	JOY_AXIS6_MINUS	=(JOY_BASE+13),
	JOY_AXIS7_PLUS	=(JOY_BASE+14),
	JOY_AXIS7_MINUS	=(JOY_BASE+15),
	JOY_AXIS8_PLUS	=(JOY_BASE+16),
	JOY_AXIS8_MINUS	=(JOY_BASE+17),
	JOY_AXIS9_PLUS	=(JOY_BASE+18),
	JOY_AXIS9_MINUS	=(JOY_BASE+19)
    };
    enum {
	JOY_BTN_BASE	=((0x100+148)|(1<<28)),
	JOY_BTN0	=(JOY_BTN_BASE+0),
	JOY_BTN1	=(JOY_BTN_BASE+1),
	JOY_BTN2	=(JOY_BTN_BASE+2),
	JOY_BTN3	=(JOY_BTN_BASE+3),
	JOY_BTN4	=(JOY_BTN_BASE+4),
	JOY_BTN5	=(JOY_BTN_BASE+5),
	JOY_BTN6	=(JOY_BTN_BASE+6),
	JOY_BTN7	=(JOY_BTN_BASE+7),
	JOY_BTN8	=(JOY_BTN_BASE+8),
	JOY_BTN9	=(JOY_BTN_BASE+9)
    };

    extern any_t* mp_input_joystick_open(const char* dev);
    extern void   mp_input_joystick_close(any_t* ctx);
    extern int    mp_input_joystick_read(any_t* ctx);
} // namespace mpxp
#endif
