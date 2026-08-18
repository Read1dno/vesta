
poll = async function () {};
loadMap = async function () {};

Promise.all([
  fetch('demo-meta.json').then(response => response.json()),
  fetch('demo-state.json').then(response => response.json())
]).then(([demoMeta, demoState]) => {
  meta = demoMeta;
  state = demoState;
  mapName = demoState.map;
  lastOk = Date.now();

  state.players.forEach((player, index) => {
    player.money = [4200, 3150, 8700, 5600, 2400][index % 5];
  });
  state.bomb = {
    active: true,
    planted: false,
    origin: [-1680, -1090, -165],
    time: 0,
    defusing: false,
    defuse: 0,
    site: 0,
    damage: -1
  };
  state.projectiles = [{
    id: 9001,
    kind: 3,
    origin: [-770, -1140, -85],
    velocity: [540, -110, 160],
    remaining: 1.6,
    effect_remaining: -1,
    detonated: false,
    smoke: false,
    bounces: 1,
    fire_points: [],
    trajectory: [
      [-1590, -520, -130], [-1470, -610, -95], [-1320, -720, -55],
      [-1150, -840, -35], [-980, -955, -52], [-840, -1065, -72], [-770, -1140, -85]
    ]
  }];

  primaryImage = new Image();
  primaryImage.src = 'assets/overview-de_mirage.png';
  rosterSequence = state.sequence;
  updateRoster();

  let phase = 0;
  setInterval(() => {
    phase += .025;
    const projectile = state.projectiles[0];
    if (projectile) projectile.remaining = 1.1 + Math.sin(phase) * .35;
  }, 40);
}).catch(() => {});
