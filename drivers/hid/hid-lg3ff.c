/*
 *  Force feedback support for Logitech Flight System G940
 *
 *  Copyright (c) 2009 Gary Stein <LordCnidarian@gmail.com>
 *  Copyright (c) 2019 Chris Boyle
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


#include <linux/input.h>
#include <linux/hid.h>

#include "../input/ff-memless-next.h"
#include "hid-lg.h"

#define FF_UPDATE_RATE 50

/* Ensure we remember to swap bytes (there's no sle16) */
typedef __s16 __bitwise lg3_s16;

static inline lg3_s16 lg3ff_cpu_to_sle16(s16 val)
{
	return (__force lg3_s16)cpu_to_le16(val);
}

struct hid_lg3ff_axis {
	lg3_s16	constant_force;  /* can cancel autocenter on relevant side */
	u8	_padding0;  /* extra byte of strength? no apparent effect */
	/* how far towards center does the effect keep pushing:
	 * 0   = no autocenter, up to:
	 * 127 = push immediately on any deflection
	 * <0  = repel center
	 */
	s8	autocenter_strength;
	/* how hard does autocenter push */
	s8	autocenter_force;
	/* damping with force of autocenter_force (see also damper_*) */
	s8	autocenter_damping;
	lg3_s16	spring_deadzone_neg;  /* for offset center, set these equal */
	lg3_s16	spring_deadzone_pos;
	s8	spring_coeff_neg;  /* <0 repels center */
	s8	spring_coeff_pos;
	lg3_s16	spring_saturation;
	u8	_padding1[8];  /* [4-8]: a different way of autocentering? */
	s8	damper_coeff_neg;
	s8	damper_coeff_pos;
	lg3_s16	damper_saturation;
	u8	_padding2[4];  /* seems to do the same as damper*? */
} __packed;

struct hid_lg3ff_report {
	struct hid_lg3ff_axis x;
	struct hid_lg3ff_axis y;
	u8	_padding[3];
} __packed;

#define FF_REPORT_ID 2

static void hig_lg3ff_send(struct input_dev *idev,
			   struct hid_lg3ff_report *raw_rep)
{
	struct hid_device *hid = input_get_drvdata(idev);
	struct hid_report *hid_rep = hid->report_enum[HID_OUTPUT_REPORT]
					 .report_id_hash[FF_REPORT_ID];
	int i;

	/* We can be called while atomic (via hid_lg3ff_play) and must queue;
	 * there's nowhere to enqueue a raw report, so populate a hid_report.
	 */
	for (i = 0; i < sizeof(*raw_rep); i++)
		hid_rep->field[0]->value[i] = ((u8 *)raw_rep)[i];
	hid_hw_request(hid, hid_rep, HID_REQ_SET_REPORT);
}

static void hid_lg3ff_clear_cond(struct hid_lg3ff_axis *axis, u16 effect_type)
{
	switch (effect_type) {
	case FF_SPRING:
		axis->spring_deadzone_neg = 0;
		axis->spring_deadzone_pos = 0;
		axis->spring_coeff_neg    = 0;
		axis->spring_coeff_pos    = 0;
		axis->spring_saturation   = 0;
		break;
	case FF_DAMPER:
		axis->damper_coeff_neg  = 0;
		axis->damper_coeff_pos  = 0;
		axis->damper_saturation = 0;
		break;
	}
}

static void hid_lg3ff_set_cond(struct hid_lg3ff_axis *axis, u16 effect_type,
				      const struct ff_condition_effect *condition)
{
	switch (effect_type) {
	case FF_SPRING:
		axis->spring_deadzone_neg = lg3ff_cpu_to_sle16(condition->center - condition->deadband / 2);
		axis->spring_deadzone_pos = lg3ff_cpu_to_sle16(condition->center + condition->deadband / 2);
		axis->spring_coeff_neg    = condition->left_coeff >> 8;
		axis->spring_coeff_pos    = condition->right_coeff >> 8;
		axis->spring_saturation   = lg3ff_cpu_to_sle16((condition->left_saturation + condition->right_saturation) / 4);
		break;
	case FF_DAMPER:
		axis->damper_coeff_neg  = condition->left_coeff >> 8;
		axis->damper_coeff_pos  = condition->right_coeff >> 8;
		axis->damper_saturation = lg3ff_cpu_to_sle16((condition->left_saturation + condition->right_saturation) / 4);
		break;
	}
}

static int hid_lg3ff_play(struct input_dev *dev, void *data,
			 const struct mlnx_effect_command *command)
{
	struct hid_lg3ff_report report = {{0}};

	switch (command->cmd) {
	case MLNX_START_COMBINED:
		/*
		 * Sign backwards from other Force3d pro
		 * which get recast here in two's complement 8 bits
		 */
		report.x.constant_force = lg3ff_cpu_to_sle16(command->u.simple_force.x);
		report.y.constant_force = lg3ff_cpu_to_sle16(command->u.simple_force.y);
		break;
	case MLNX_STOP_COMBINED:
		report.x.constant_force = 0;
		report.y.constant_force = 0;
		break;
	case MLNX_UPLOAD_UNCOMB:
		switch (command->u.uncomb.effect->type) {
			case FF_SPRING:
			case FF_DAMPER:
				return 0;
			default:
				return -EINVAL;
		}
	case MLNX_START_UNCOMB:
		hid_lg3ff_set_cond(&report.x, command->u.uncomb.effect->type,
				   &command->u.uncomb.effect->u.condition[0]);
		hid_lg3ff_set_cond(&report.y, command->u.uncomb.effect->type,
				   &command->u.uncomb.effect->u.condition[1]);
		break;
	case MLNX_STOP_UNCOMB:
		hid_lg3ff_clear_cond(&report.x, command->u.uncomb.effect->type);
		hid_lg3ff_clear_cond(&report.y, command->u.uncomb.effect->type);
		break;
	default:
		return -EINVAL;
	}

	hig_lg3ff_send(dev, &report);

	return 0;
}

static void hid_lg3ff_set_autocenter(struct input_dev *dev, u16 magnitude)
{
	struct hid_lg3ff_report report = {{0}};

	/* negative means repel from center, so scale to 0-127 */
	s8 mag_scaled = magnitude >> 9;

	report.x.autocenter_strength = 127;
	report.x.autocenter_force = mag_scaled;
	report.y.autocenter_strength = 127;
	report.y.autocenter_force = mag_scaled;
	hig_lg3ff_send(dev, &report);
}


static const signed short ff3_joystick_ac[] = {
	FF_CONSTANT,
	FF_RAMP,
	FF_PERIODIC,
	FF_SQUARE,
	FF_TRIANGLE,
	FF_SINE,
	FF_SAW_UP,
	FF_SAW_DOWN,
	FF_SPRING,
	FF_DAMPER,
	FF_AUTOCENTER,
	-1
};

int lg3ff_init(struct hid_device *hid)
{
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct input_dev *dev = hidinput->input;
	const signed short *ff_bits = ff3_joystick_ac;
	int error;
	int i;

	/* Check that the report looks ok */
	BUILD_BUG_ON(sizeof(struct hid_lg3ff_report) != 63);  /* excl. id */
	if (!hid_validate_values(hid, HID_OUTPUT_REPORT, FF_REPORT_ID, 0,
				 sizeof(struct hid_lg3ff_report)))
		return -ENODEV;

	/* Assume single fixed device G940 */
	for (i = 0; ff_bits[i] >= 0; i++)
		set_bit(ff_bits[i], dev->ffbit);

	error = input_ff_create_mlnx(dev, NULL, hid_lg3ff_play, FF_UPDATE_RATE);
	if (error)
		return error;

	if (test_bit(FF_AUTOCENTER, dev->ffbit)) {
		dev->ff->set_autocenter = hid_lg3ff_set_autocenter;
		hid_lg3ff_set_autocenter(dev, 0);
	}

	hid_info(hid, "Force feedback for Logitech Flight System G940 by Gary Stein <LordCnidarian@gmail.com>\n");
	return 0;
}

