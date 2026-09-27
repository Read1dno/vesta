(() => {
  const translations = {
    en: {
      'nav.features':'Features','nav.configs':'Configs','nav.download':'Download','nav.source':'Source',
      'hero.kicker':'Open source · Windows x64','hero.title':'The best external cheat for Counter-Strike 2.','hero.lede':'Read-only architecture, a unified combat pipeline, fully configurable visuals and a local Lua API—built as one coherent product.','hero.release':'Get the latest release','hero.config':'Choose a config','hero.principle1':'Read-only process access','hero.principle2':'No driver or injector','hero.principle3':'No telemetry',
      'features.kicker':'The product','features.title':'Everything Vesta includes.','features.lede':'Explore every configurable setting from the current Vesta profile. The tree is generated directly from the product configuration.','features.combatTitle':'Combat','features.combatText':'Predictive aim, humanization, RCS, trigger modes, penetration and seed-aware timing through one pipeline.','features.visualsTitle':'Visuals','features.visualsText':'Per-pixel visible and invisible chams, player ESP, radar, projectile prediction, bomb information and effects.','features.utilityTitle':'Utility','features.utilityText':'Grenade lineups, movement tools, auto accept, spectator-aware presentation and configurable overlay widgets.','features.localTitle':'Local by default','features.localText':'The native application contains no networking path. Optional networked tools live in user-loaded Lua scripts.',
      'configs.kicker':'Ready-made profiles','configs.title':'Start with a coherent config, then make it yours.','configs.lede':'Every profile is a regular Vesta configuration file. Download it, open Configs in Vesta and load the file—nothing is installed or executed.','configs.fullType':'Information parity','configs.fullText':'Legit Sync ESP and basic utilities only—no information the game has not legitimately exposed.','configs.legitType':'Play against WH','configs.legitText':'Wall information and a basic trigger configuration for matches where opponents already use WH.','configs.semiType':'Maximum output','configs.semiText':'Seed Trigger tuned for jump shots, aggressive timing and the highest practical effectiveness.','configs.download':'Download .cfg',
      'lua.title':'Build your own tools on top of Vesta.','lua.lede':'Scripts receive documented game snapshots, UI controls, drawing primitives and helper APIs. Web Radar is the first complete example—and the preview below uses its real frontend and a captured Vesta snapshot.','lua.download':'Download Web Radar.lua','lua.docs':'Read API documentation',
      'download.kicker':'Get Vesta','download.title':'Download the current release or inspect every line.','download.lede':'Release builds and source live on GitHub. The landing page is static and can be hosted directly with GitHub Pages.','download.release':'Latest release','download.source':'View source',
      'support.title':'Support development','support.lede':'Optional ways to support continued open-source work.','support.russia':'Russia','support.service':'Donation service','support.note':'Set donation destinations in site-config.js.','footer.tagline':'Open source. Local-first. Built for deliberate control.','footer.back':'Back to top','copy.copied':'Copied',
      title:'Vesta — open-source external cheat', description:'Vesta is an open-source, read-only external cheat for Counter-Strike 2.'
    },
    ru: {
      'nav.features':'Возможности','nav.configs':'Конфиги','nav.download':'Скачать','nav.source':'Исходный код',
      'hero.kicker':'Открытый код · Windows x64','hero.title':'Лучший внешний чит для Counter-Strike 2.','hero.lede':'Read-only архитектура, единый combat pipeline, полностью настраиваемые визуалы и локальный Lua API в одном цельном продукте.','hero.release':'Скачать последнюю версию','hero.config':'Выбрать конфиг','hero.principle1':'Read-only доступ к процессу','hero.principle2':'Без драйвера и инжектора','hero.principle3':'Без телеметрии',
      'features.kicker':'Продукт','features.title':'Все возможности Vesta.','features.lede':'Изучите каждую настраиваемую функцию актуального профиля Vesta. Дерево собирается напрямую из конфигурации продукта.','features.combatTitle':'Combat','features.combatText':'Предиктивный aim, humanizer, RCS, режимы trigger, penetration и seed-aware тайминги в едином pipeline.','features.visualsTitle':'Визуалы','features.visualsText':'Попиксельные visible/invisible chams, player ESP, radar, предикт снарядов, информация о бомбе и эффекты.','features.utilityTitle':'Инструменты','features.utilityText':'Гранатные lineup, movement-функции, auto accept, spectator sync и настраиваемые виджеты оверлея.','features.localTitle':'Локальный по умолчанию','features.localText':'В нативном приложении нет сетевого пути. Сетевые инструменты доступны только через загруженные пользователем Lua-скрипты.',
      'configs.kicker':'Готовые профили','configs.title':'Начните с цельного конфига и настройте его под себя.','configs.lede':'Каждый профиль — обычный файл конфигурации Vesta. Скачайте его, откройте Configs в Vesta и загрузите файл: ничего не устанавливается и не запускается.','configs.fullType':'Только легитная информация','configs.fullText':'Только Legit Sync ESP и базовые функции — без информации, которую игра не показала легитимным способом.','configs.legitType':'Для игры против WH','configs.legitText':'Wallhack и базовый trigger для матчей, где противники уже используют WH.','configs.semiType':'Максимальная результативность','configs.semiText':'Seed Trigger для убийств в прыжке, агрессивные тайминги и максимальная практическая эффективность.','configs.download':'Скачать .cfg',
      'lua.title':'Создавайте собственные инструменты поверх Vesta.','lua.lede':'Скриптам доступны документированные game snapshots, UI-контролы, примитивы отрисовки и helper API. Web Radar — первый полноценный пример; превью ниже использует его настоящий frontend и сохранённый snapshot Vesta.','lua.download':'Скачать Web Radar.lua','lua.docs':'Открыть документацию API',
      'download.kicker':'Скачать Vesta','download.title':'Скачайте актуальный релиз или изучите каждую строку кода.','download.lede':'Релизы и исходный код размещены на GitHub. Лендинг полностью статический и публикуется через GitHub Pages.','download.release':'Последний релиз','download.source':'Открыть исходный код',
      'support.title':'Поддержать разработку','support.lede':'Необязательные способы поддержать развитие open-source проекта.','support.russia':'Россия','support.service':'Сервис донатов','support.note':'Укажите способы поддержки в site-config.js.','footer.tagline':'Открытый код. Локальная работа. Полный контроль.','footer.back':'Наверх','copy.copied':'Скопировано',
      title:'Vesta — внешний open-source чит', description:'Vesta — внешний read-only чит с открытым исходным кодом для Counter-Strike 2.'
    }
  };
  const bilingual = (en, ru) => ({ en, ru });
  const legacyFeatureGraph = [
    {
      id:'aimbot', title:bilingual('Aimbot','Аимбот'), description:bilingual('Target acquisition, prediction and recoil compensation share one combat solution.','Выбор цели, предикт и компенсация отдачи работают через единое combat-решение.'),
      children:[
        {title:bilingual('Activation','Активация'), description:bilingual('Master switch, Hold or Always mode, configurable hotkey.','Мастер-свич, режим удержания или постоянной работы, настраиваемый бинд.')},
        {title:bilingual('Checks','Проверки'), children:[
          {title:bilingual('Airborne','Прыжок'), description:bilingual('Block while the local player is airborne.','Блокировка, когда локальный игрок находится в воздухе.')},
          {title:bilingual('Flash','Ослепление'), description:bilingual('Configurable flash threshold based on the actual flash state.','Настраиваемый порог по фактическому состоянию ослепления.')},
          {title:bilingual('Smoke','Дым'), description:bilingual('Voxel/coarse smoke intersection check.','Проверка пересечения луча с voxel/coarse объёмом дыма.')}
        ]},
        {title:bilingual('Targeting','Выбор цели'), children:[
          {title:bilingual('Hitboxes','Хитбоксы'), description:bilingual('Head, body and limb groups with priority selection.','Группы головы, корпуса и конечностей с выбором приоритета.')},
          {title:bilingual('FOV','FOV'), children:[
            {title:bilingual('Fixed','Фиксированный'), description:bilingual('Static angular selection circle.','Статический угловой круг выбора цели.')},
            {title:bilingual('Distance','По дистанции'), description:bilingual('Near/far bounds with a smooth distance curve.','Ближняя и дальняя границы с плавной кривой по дистанции.')},
            {title:bilingual('Target','На цели'), description:bilingual('A single distance-aware circle bound to the nearest valid hitbox.','Один зависящий от дистанции круг на ближайшем валидном хитбоксе.')}
          ]},
          {title:bilingual('Multipoint','Мультипоинт'), description:bilingual('Center, cap and side points with separate head/body/limb scales.','Центр, верхние и боковые точки с отдельным масштабом головы, тела и конечностей.')},
          {title:bilingual('Prediction','Предикт'), description:bilingual('Simulation-tick velocity, sample age and bounded extrapolation.','Скорость по simulation tick, возраст snapshot и ограниченная экстраполяция.')},
          {title:bilingual('Lethal only','Только летальный урон'), description:bilingual('Reject non-lethal body points while retaining head priority.','Исключает несмертельные точки тела, сохраняя голову приоритетной.')},
          {title:bilingual('Minimum damage','Минимальный урон'), description:bilingual('Damage threshold shared with penetration checks.','Порог урона, общий с проверкой пенетрации.')}
        ]},
        {title:bilingual('Humanizer','Хуманайзер'), children:[
          {title:bilingual('Smoothing','Сглаживание'), description:bilingual('Time-based aim response and zero-smoothing snap mode.','Временная скорость доводки и мгновенный режим при нулевом сглаживании.')},
          {title:bilingual('WindMouse profile','Профиль WindMouse'), description:bilingual('Gravity, wind, damping, max step and reaction range.','Гравитация, ветер, damping, максимальный шаг и диапазон реакции.')},
          {title:bilingual('Motion details','Детали движения'), description:bilingual('Curve, overshoot, jitter and deadzone.','Кривая, overshoot, jitter и deadzone.')}
        ]},
        {title:bilingual('RCS','RCS'), children:[
          {title:bilingual('Compensation','Компенсация'), description:bilingual('Independent pitch/yaw strength and start bullet.','Отдельная сила pitch/yaw и номер первой компенсируемой пули.')},
          {title:bilingual('Response','Отклик'), description:bilingual('Humanized response time, randomness and drift.','Humanized время реакции, рандомизация и drift.')},
          {title:bilingual('Aimbot integration','Совместная работа с аимом'), description:bilingual('Recoil offset is applied to the predicted target without changing aim speed.','Отдача компенсируется относительно предикта цели, не меняя скорость аима.')}
        ]},
        {title:bilingual('Penetration','Пенетрация'), description:bilingual('Visible-only policy, material-aware ballistics and minimum damage.','Visible-only политика, баллистика с учётом материала и минимального урона.')},
        {title:bilingual('Grenade aim','Наведение гранат'), description:bilingual('Separate FOV, hotkey and smoothing for lineup assistance.','Отдельные FOV, бинд и сглаживание для помощи с lineup.')}
      ]
    },
    {
      id:'triggerbot', title:bilingual('Triggerbot','Триггербот'), description:bilingual('Simple reaction timing, advanced ballistics and deterministic Seed Trigger.','Обычные тайминги реакции, расширенная баллистика и детерминированный Seed Trigger.'),
      children:[
        {title:bilingual('Activation','Активация'), description:bilingual('Master switch, Hold or Always mode and a direct-access hotkey.','Мастер-свич, режим удержания или постоянной работы и бинд в быстром доступе.')},
        {title:bilingual('Core','Основные настройки'), children:[
          {title:bilingual('Shot delay','Задержка выстрела'), description:bilingual('Delay after acquisition before the first shot.','Задержка между обнаружением цели и первым выстрелом.')},
          {title:bilingual('Post-shot delay','Задержка после выстрела'), description:bilingual('Lockout window that prevents duplicate shots.','Окно блокировки, исключающее повторный выстрел.')},
          {title:bilingual('Timing humanization','Хуманизация тайминга'), description:bilingual('Symmetric variance plus configurable rare outliers.','Симметричный разброс задержки и редкие настраиваемые выбросы.')},
          {title:bilingual('Hitboxes & lethal only','Хитбоксы и lethal only'), description:bilingual('Per-group filtering with head fallback when no body point is lethal.','Фильтры по группам с выстрелом в голову, если тело не даёт lethal.')}
        ]},
        {title:bilingual('Advanced mode','Расширенный режим'), description:bilingual('Hitchance, minimum damage and predictive target evaluation.','Hitchance, минимальный урон и предиктивная оценка цели.')},
        {title:bilingual('Seed Trigger','Seed Trigger'), children:[
          {title:bilingual('Tick + 1 solution','Решение на тик + 1'), description:bilingual('Dedicated high-priority snapshot, decision and input pipeline.','Отдельный high-priority pipeline чтения, решения и ввода.')},
          {title:bilingual('Spread & recoil','Spread и recoil'), description:bilingual('Deterministic RNG with current weapon recoil state.','Детерминированный RNG с текущим состоянием отдачи оружия.')},
          {title:bilingual('Airborne shots','Выстрелы в прыжке'), description:bilingual('Fires only on a seed that intersects the selected hitbox.','Стреляет только на seed, пересекающем выбранный хитбокс.')}
        ]},
        {title:bilingual('Checks','Проверки'), description:bilingual('Airborne, flash, smoke, immunity and weapon readiness.','Прыжок, ослепление, дым, иммунитет и готовность оружия.')},
        {title:bilingual('Penetration','Пенетрация'), description:bilingual('Material-aware damage with dynamic door and breakable collision.','Урон по материалам с коллизией динамических дверей и разрушаемых объектов.')},
        {title:bilingual('Predictive Auto Stop','Предиктивный Auto Stop'), description:bilingual('Counter-strafe scheduled for the first accurate shot window.','Контрстрейф рассчитывается к первому точному окну выстрела.')}
      ]
    },
    {
      id:'visuals', title:bilingual('Visuals','Визуалы'), description:bilingual('Players, world information, radar and presentation use one synchronized snapshot.','Игроки, информация о мире, радар и эффекты используют один синхронизированный snapshot.'),
      children:[
        {title:bilingual('Players','Игроки'), children:[
          {title:bilingual('Player ESP','Player ESP'), children:[
            {title:bilingual('ESP Editor','ESP Editor'), description:bilingual('Box, health, armor, name, weapon, ammo mask and info flags.','Box, здоровье, броня, имя, оружие, ammo mask и информационные флаги.')},
            {title:bilingual('Geometry','Геометрия'), description:bilingual('Skeleton, head circle, hitboxes and view line.','Скелет, круг головы, хитбоксы и линия взгляда.')},
            {title:bilingual('Awareness','Информация'), description:bilingual('Offscreen arrows, sound ESP and threat display.','Offscreen arrows, sound ESP и отображение угрозы.')}
          ]},
          {title:bilingual('Legit Sync','Legit Sync'), description:bilingual('Direct visibility, radar spotted state and sound hold windows.','Фактическая видимость, radar spotted state и окна отображения по звуку.')},
          {title:bilingual('Spectator Sync','Spectator Sync'), description:bilingual('Immediately hides Player ESP while somebody observes you.','Мгновенно скрывает Player ESP, когда за вами наблюдают.')},
          {title:bilingual('Chams','Chams'), children:[
            {title:bilingual('Visible / invisible','Visible / invisible'), description:bilingual('Per-pixel world-depth classification with independent materials.','Попиксельная world-depth классификация с отдельными материалами.')},
            {title:bilingual('Appearance','Материалы'), description:bilingual('Color, Fresnel, roughness, metalness, wireframe and anti-aliasing.','Цвет, Fresnel, roughness, metalness, wireframe и антиалиасинг.')},
            {title:bilingual('Effects','Эффекты'), description:bilingual('Bloom, on-hit chams and model-shaped kill particles.','Bloom, on-hit chams и частицы смерти по форме модели.')}
          ]}
        ]},
        {title:bilingual('Items','Предметы'), description:bilingual('Weapon and utility filters, names, icons, ammo and draw distance.','Фильтры оружия и utility, названия, иконки, патроны и дистанция.')},
        {title:bilingual('Projectiles','Гранаты в мире'), description:bilingual('Type icons, timer rings, predicted paths and inferno bounds.','Иконки типов, таймеры, предсказанные траектории и границы inferno.')},
        {title:bilingual('Bomb','Бомба'), description:bilingual('World marker, compact panel, explosion/defuse contour and damage safe zone.','Метка в мире, компактная панель, контур взрыва/дефуза и safe zone урона.')},
        {title:bilingual('Radar','Радар'), children:[
          {title:bilingual('Players & loadout','Игроки и loadout'), description:bilingual('Teams, view direction, health, armor, weapons, grenades and economy.','Команды, направление взгляда, здоровье, броня, оружие, гранаты и экономика.')},
          {title:bilingual('World state','Состояние мира'), description:bilingual('Bomb, sites, projectiles, paths, timers and inferno bounds.','Бомба, пленты, гранаты, траектории, таймеры и inferno bounds.')},
          {title:bilingual('Web Radar','Web Radar'), description:bilingual('Optional Lua package with a private browser link.','Опциональный Lua-пакет с приватной ссылкой для браузера.')}
        ]},
        {title:bilingual('Effects','Эффекты'), description:bilingual('Bullet tracers, hit feedback, event cards and No Flash.','Bullet tracers, hit feedback, карточки событий и No Flash.')},
        {title:bilingual('Crosshair','Прицел'), description:bilingual('Custom crosshair, exact in-game sync and penetration indicator.','Собственный прицел, точная синхронизация с игровым и индикатор пенетрации.')}
      ]
    },
    {
      id:'misc', title:bilingual('Misc','Разное'), description:bilingual('Interface, grenade workflows, movement, overlay widgets, Lua and profiles.','Интерфейс, работа с гранатами, movement, виджеты, Lua и профили.'),
      children:[
        {title:bilingual('General','Основное'), description:bilingual('Language, DPI scale, FPS limit, auto accept and overlay engine.','Язык, DPI scale, лимит FPS, auto accept и движок оверлея.')},
        {title:bilingual('Grenades','Гранаты'), children:[
          {title:bilingual('Nade Helper','Nade Helper'), description:bilingual('Lineup database, stand marker, aim assist and action guidance.','База lineup, точка позиции, aim assist и подсказка действия.')},
          {title:bilingual('Auto Release','Auto Release'), description:bilingual('Walk, jump and release sequence for supported lineups.','Последовательность движения, прыжка и броска для поддерживаемых lineup.')},
          {title:bilingual('Trajectory','Траектория'), description:bilingual('Thickness, colors, bounces, endpoint and bloom.','Толщина, цвета, точки отскока, конечная точка и bloom.')}
        ]},
        {title:bilingual('Movement','Movement'), children:[
          {title:bilingual('Bunny Hop','Bunny Hop'), description:bilingual('Binding-aware automatic jump timing.','Автоматические прыжки с учётом актуального бинда.')},
          {title:bilingual('Edge Jump','Edge Jump'), description:bilingual('Jump on the transition from ground to air.','Прыжок в момент перехода с земли в воздух.')},
          {title:bilingual('Auto Stop','Auto Stop'), description:bilingual('Physical-fire and predictive-trigger stopping modes.','Остановка для ручной стрельбы и предиктивного trigger.')}
        ]},
        {title:bilingual('Overlay','Оверлей'), description:bilingual('Watermark, spectator list, hitmarker, hitsound, damage popup and event log.','Watermark, spectator list, hitmarker, hitsound, damage popup и event log.')},
        {title:bilingual('Lua API','Lua API'), description:bilingual('Script packages, hot reload, UI/drawing API and Web Radar.','Пакеты скриптов, hot reload, UI/drawing API и Web Radar.')},
        {title:bilingual('Configs','Конфиги'), description:bilingual('Create, import, export and switch complete profiles.','Создание, импорт, экспорт и переключение полных профилей.')}
      ]
    }
  ];
  const featureGraph = window.VESTA_FEATURE_TREE || [];
  const localized = value => typeof value === 'string' ? value : value?.[language] || value?.en || '';
  const treeLabels = {
    en: { Aimbot:'Aimbot', Triggerbot:'Triggerbot', Visuals:'Visuals', Misc:'Misc', Global:'Global', 'Weapon overrides':'Weapon overrides', Pistol:'Pistol', SMG:'SMG', Rifle:'Rifle', Shotgun:'Shotgun', Sniper:'Sniper', Heavy:'Heavy' },
    ru: { Aimbot:'Аимбот', Triggerbot:'Триггербот', Visuals:'Визуалы', Misc:'Разное', Global:'Общие настройки', 'Weapon overrides':'Профили оружия', Pistol:'Пистолеты', SMG:'Пистолеты-пулемёты', Rifle:'Винтовки', Shotgun:'Дробовики', Sniper:'Снайперские винтовки', Heavy:'Тяжёлое оружие' }
  };
  const ruWords = {
    enabled:'включение', enable:'включение', activation:'активация', mode:'режим', key:'бинд', always:'всегда', checks:'проверки', airborne:'прыжок', flash:'ослепление', flashed:'ослепление', smoke:'дым', walls:'стены', wall:'стена', visible:'видимый', invisible:'невидимый', draw:'отрисовка', color:'цвет', background:'фон', outline:'обводка', thickness:'толщина', size:'размер', scale:'масштаб', distance:'дистанция', near:'ближний', far:'дальний', radius:'радиус', curve:'кривая', selection:'выбор', visualization:'визуализация', hitbox:'хитбокс', hitboxes:'хитбоксы', damage:'урон', minimum:'минимальный', min:'минимум', maximum:'максимальный', max:'максимум', delay:'задержка', duration:'длительность', speed:'скорость', amount:'значение', prediction:'предикт', multipoint:'мультипоинт', humanizer:'хуманайзер', smoothing:'сглаживание', recoil:'отдача', penetration:'пенетрация', player:'игрок', players:'игроки', weapon:'оружие', weapons:'оружие', health:'здоровье', armor:'броня', ammo:'патроны', name:'название', box:'бокс', skeleton:'скелет', flags:'флаги', radar:'радар', grenade:'граната', grenades:'гранаты', bomb:'бомба', trajectory:'траектория', endpoint:'конечная точка', bounce:'отскок', bloom:'свечение', sound:'звук', volume:'громкость', marker:'маркер', event:'событие', log:'лог', interface:'интерфейс', overlay:'оверлей', spectator:'наблюдатель', list:'список', auto:'авто', release:'бросок', stop:'остановка', jump:'прыжок', movement:'движение', config:'конфиг', configs:'конфиги', profile:'профиль', use:'использовать', global:'общие', only:'только', lethal:'летальный', show:'показывать', style:'стиль', alpha:'прозрачность', opacity:'прозрачность', line:'линия', point:'точка', points:'точки', material:'материал', world:'мир', effects:'эффекты', chams:'чамсы', crosshair:'прицел', sync:'синхронизация', legit:'легит', direct:'прямая', target:'цель', team:'команда', enemy:'противник', local:'локальный', custom:'пользовательский', random:'случайный', chance:'шанс', threshold:'порог', response:'отклик', gain:'усиление', drift:'дрейф', start:'начало', bullet:'пуля', bullets:'пули', time:'время', offset:'смещение', width:'ширина', height:'высота', position:'позиция', text:'текст', icon:'иконка', icons:'иконки', font:'шрифт', frame:'кадр', latency:'задержка', priority:'приоритет'
  };
  const acronyms = new Set(['fov','rcs','esp','dpi','fps','msaa','hp','lua','obs','ui','id','rgba','rpm','smg']);
  const formatTreeLabel = key => {
    const exact = treeLabels[language]?.[key];
    if (exact) return exact;
    const parts = String(key).replace(/^m_/, '').split('_').filter(Boolean);
    const words = parts.map(part => {
      const lower = part.toLowerCase();
      if (acronyms.has(lower)) return lower.toUpperCase();
      if (language === 'ru' && ruWords[lower]) return ruWords[lower];
      return lower;
    });
    const label = words.join(' ');
    return label ? label[0].toUpperCase() + label.slice(1) : String(key);
  };
  const createFeatureNode = (node, depth = 0, index = 0) => {
    if (!node.children?.length) {
      const leaf = document.createElement('div');
      leaf.className = 'config-tree-row config-tree-leaf';
      leaf.setAttribute('role', 'treeitem');
      const label = document.createElement('span');
      label.className = 'config-tree-label';
      label.textContent = formatTreeLabel(node.key || node.title);
      leaf.append(label);
      return leaf;
    }
    const branch = document.createElement('details');
    branch.className = `config-tree-branch${depth === 0 ? ' config-tree-root' : ''}`;
    branch.open = depth === 0;
    const summary = document.createElement('summary');
    summary.className = 'config-tree-row';
    summary.setAttribute('role', 'treeitem');
    const chevron = document.createElement('span');
    chevron.className = 'config-tree-chevron';
    const label = document.createElement('span');
    label.className = 'config-tree-label';
    label.textContent = formatTreeLabel(node.key || node.title);
    summary.append(chevron, label);
    const children = document.createElement('div');
    children.className = 'config-tree-children';
    children.setAttribute('role', 'group');
    node.children.forEach((child, childIndex) => children.append(createFeatureNode(child, depth + 1, childIndex)));
    branch.append(summary, children);
    return branch;
  };
  const renderFeatureGraph = () => {
    const map = document.querySelector('[data-feature-map]');
    if (!map) return;
    const tree = document.createElement('div');
    tree.className = 'config-tree';
    tree.setAttribute('role', 'tree');
    featureGraph.forEach((section, index) => {
      const root = createFeatureNode(section, 0, index);
      tree.append(root);
    });
    map.replaceChildren(tree);
  };
  const savedLanguage = localStorage.getItem('vesta-language');
  const preferredLanguage = savedLanguage || ((navigator.languages || [navigator.language]).some(item => item?.toLowerCase().startsWith('ru')) ? 'ru' : 'en');
  let language = preferredLanguage;
  const applyLanguage = nextLanguage => {
    language = translations[nextLanguage] ? nextLanguage : 'en';
    localStorage.setItem('vesta-language', language);
    document.documentElement.lang = language;
    const dictionary = translations[language];
    document.querySelectorAll('[data-i18n]').forEach(element => {
      const value = dictionary[element.dataset.i18n];
      if (value) element.textContent = value;
    });
    const languageButton = document.querySelector('[data-language-toggle]');
    if (languageButton) {
      languageButton.textContent = language.toUpperCase();
      languageButton.setAttribute('aria-label', language === 'en' ? 'Switch to Russian' : 'Переключить на английский');
      languageButton.title = language === 'en' ? 'Русский' : 'English';
    }
    document.querySelectorAll('[data-api-link]').forEach(link => { link.href = `docs/api_docs/${language}/`; });
    document.title = dictionary.title;
    document.querySelector('meta[name="description"]')?.setAttribute('content', dictionary.description);
    document.querySelector('meta[property="og:title"]')?.setAttribute('content', dictionary.title);
    document.querySelector('meta[property="og:description"]')?.setAttribute('content', dictionary.description);
    renderFeatureGraph();
  };
  document.querySelector('[data-language-toggle]')?.addEventListener('click', () => applyLanguage(language === 'en' ? 'ru' : 'en'));
  applyLanguage(language);

  const config = window.VESTA_SITE || {};
  const repoFromPages = (() => {
    const host = location.hostname;
    if (!host.endsWith('.github.io')) return null;
    const owner = host.slice(0, -'.github.io'.length);
    const repo = location.pathname.split('/').filter(Boolean)[0];
    return repo ? `https://github.com/${owner}/${repo}` : `https://github.com/${owner}`;
  })();
  const sourceUrl = config.sourceUrl || repoFromPages || '#download';
  const releaseUrl = config.releaseUrl || (repoFromPages ? `${repoFromPages}/releases/latest` : '#download');

  document.querySelectorAll('[data-source-link]').forEach(link => link.href = sourceUrl);
  document.querySelectorAll('[data-release-link]').forEach(link => link.href = releaseUrl);

  const header = document.querySelector('[data-header]');
  const nav = document.querySelector('[data-nav]');
  const menuButton = document.querySelector('[data-menu-button]');
  const updateHeader = () => header?.classList.toggle('scrolled', scrollY > 14);
  updateHeader();
  addEventListener('scroll', updateHeader, {passive:true});
  menuButton?.addEventListener('click', () => {
    const open = nav.classList.toggle('open');
    document.body.classList.toggle('nav-open', open);
    menuButton.setAttribute('aria-expanded', String(open));
  });
  nav?.querySelectorAll('a').forEach(link => link.addEventListener('click', () => {
    nav.classList.remove('open');
    document.body.classList.remove('nav-open');
    menuButton?.setAttribute('aria-expanded', 'false');
  }));
  addEventListener('keydown', event => {
    if (event.key !== 'Escape' || !nav?.classList.contains('open')) return;
    nav.classList.remove('open');
    document.body.classList.remove('nav-open');
    menuButton?.setAttribute('aria-expanded', 'false');
    menuButton?.focus();
  });

  const observer = new IntersectionObserver(entries => {
    entries.forEach(entry => { if (entry.isIntersecting) { entry.target.classList.add('visible'); observer.unobserve(entry.target); } });
  }, {threshold:.08, rootMargin:'0px 0px -40px'});
  document.querySelectorAll('.reveal').forEach(element => observer.observe(element));

})();
