import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";

const clone = value => JSON.parse(JSON.stringify(value));

function configure(base, changes) {
  const result = clone(base);
  const combat = result.combat_global;
  for (const [key, value] of Object.entries(changes)) {
    if (!(key in combat)) throw new Error(`Unknown combat config key: ${key}`);
    combat[key] = value;
  }
  return result;
}

export async function generateConfigPresets(projectRoot, destination) {
  const source = JSON.parse(await readFile(join(projectRoot, "configs", "legit.cfg"), "utf8"));
  const fullLegit = configure(source, {
    aimbot_enabled: false,
    aimbot_visible_only: true,
    grenade_aim_enabled: false,
    penetration_crosshair: false,
    triggerbot_enabled: false,
    triggerbot_autostop: false
  });
  fullLegit.esp.m_player.enabled = true;
  fullLegit.esp.m_player.m_legit_sync.enabled = true;
  fullLegit.esp.m_chams.enabled = false;
  fullLegit.esp.m_item.enabled = false;
  fullLegit.esp.m_projectile.enabled = false;
  fullLegit.esp.m_bomb.enabled = false;
  fullLegit.esp.m_radar.enabled = false;
  fullLegit.esp.m_sound.enabled = false;
  fullLegit.esp.m_no_flash.enabled = false;
  fullLegit.misc.m_auto_stop.enabled = false;
  fullLegit.misc.m_bunny_hop.enabled = false;
  fullLegit.misc.m_edge_jump.enabled = false;
  fullLegit.misc.m_grenades.enabled = false;
  fullLegit.misc.m_nade_helper.enabled = false;
  fullLegit.misc.m_bullet_tracers.enabled = false;

  const legit = configure(source, {
    aimbot_enabled: false,
    grenade_aim_enabled: false,
    penetration_crosshair: false,
    triggerbot_enabled: true,
    triggerbot_seed_type: 0,
    triggerbot_delay: 35,
    triggerbot_delay_after_ms: 150,
    triggerbot_hitchance: 0,
    triggerbot_min_damage: 1,
    triggerbot_lethal_only: false,
    triggerbot_autowall: false,
    triggerbot_autostop: false,
    triggerbot_randomize_ms: 0,
    triggerbot_outlier_chance: 0
  });
  legit.esp.m_player.m_legit_sync.enabled = false;
  legit.esp.m_chams.enabled = true;
  legit.esp.m_chams.invisible.enabled = true;
  legit.misc.m_auto_stop.enabled = false;
  legit.misc.m_bunny_hop.enabled = false;
  legit.misc.m_edge_jump.enabled = false;
  legit.misc.m_grenades.enabled = false;
  legit.misc.m_nade_helper.enabled = false;

  const semiRage = configure(source, {
    aimbot_enabled: true,
    aimbot_fov: 18,
    aimbot_humanize: 0,
    aimbot_smoothing: 0,
    aimbot_min_damage: 1,
    aimbot_multipoint: true,
    aimbot_visible_only: false,
    aimbot_checks: { airborne: false, flash_threshold: 100, flash_threshold_percent: 100, flashed: false, smoke: false, walls: 0 },
    triggerbot_enabled: true,
    triggerbot_delay: 0,
    triggerbot_delay_after_ms: 0,
    triggerbot_hitchance: 0,
    triggerbot_min_damage: 1,
    triggerbot_lethal_only: false,
    triggerbot_seed_type: 2,
    triggerbot_predictive: true,
    triggerbot_autowall: true,
    triggerbot_autostop: false,
    triggerbot_randomize_ms: 0,
    triggerbot_outlier_chance: 0,
    triggerbot_outlier_delay_ms: 0
  });
  semiRage.combat_global.aimbot_fov_config = {
    ...semiRage.combat_global.aimbot_fov_config,
    near_distance_m: 1.5,
    near_fov: 45,
    far_distance_m: 50,
    far_fov: 12
  };
  semiRage.esp.m_player.m_legit_sync.enabled = false;
  semiRage.misc.m_auto_stop.enabled = false;

  await mkdir(destination, { recursive: true });
  for (const [name, profile] of Object.entries({
    "full-legit.cfg": fullLegit,
    "legit.cfg": legit,
    "semi-rage.cfg": semiRage
  })) {
    await writeFile(join(destination, name), `${JSON.stringify(profile, null, 4)}\n`, "utf8");
  }
}
