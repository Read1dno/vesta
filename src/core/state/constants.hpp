#pragma once

namespace game::rules {

	constexpr auto simulation_step = 1.0f / 64.0f;

	enum joint_id : std::uint32_t
	{
		origin = 0,
		pelvis = 1,
		spine_0 = 2,
		spine_1 = 3,
		spine_2 = 4,
		spine_3 = 5,
		neck = 6,
		head = 7,
		clavicle_l = 8,
		shoulder_l = 9,
		elbow_l = 10,
		hand_l = 11,
		clavicle_r = 12,
		shoulder_r = 13,
		elbow_r = 14,
		hand_r = 15,
		hip_l = 17,
		knee_l = 18,
		foot_heel_l = 19,
		hip_r = 20,
		knee_r = 21,
		foot_heel_r = 22,
		chest = 23,
		gun = 24,
		eye_l = 25,
		eye_r = 26,
		random_bone = 27,
		cvj_bone = 28,
		foot_toes_l_t = 74,
		foot_toes_r_t = 77,
		foot_toes_l_ct = 81,
		foot_toes_r_ct = 86,
		bone_max = 128
	};

	enum equipment_class : std::uint32_t
	{
		knife,
		sidearm,
		smg,
		rifle,
		shotgun,
		precision,
		lmg,
		objective,
		electroshock,
		throwable,
		equipment,
		healthshot
	};

	constexpr inline bool is_firearm( std::uint32_t equipment_type ) { return equipment_type >= 1 && equipment_type <= 6; }

	enum input_button : std::uintptr_t
	{
		in_attack = 1 << 0,
		in_jump = 1 << 1,
		duck = 1 << 2,
		in_forward = 1 << 3,
		in_back = 1 << 4,
		in_use = 1 << 5,
		in_left = 1 << 7,
		in_right = 1 << 8,
		in_moveleft = 1 << 9,
		in_moveright = 1 << 10,
		in_second_attack = 1 << 11,
		in_reload = 1 << 13,
		sprint = 1 << 16,
		in_joyautosprint = 1 << 17,
		in_showscores = 1ULL << 33,
		in_zoom = 1ULL << 34,
		in_lookatweapon = 1ULL << 35
	};

}
