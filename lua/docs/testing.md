# Проверка Lua runtime

Файлы в `examples/` покрывают ручной интеграционный тест диагностической сборки:

- `smoke.lua` — API version, controls, events, config
  schema, storage и drawing;
- `invalid-hot-reload.lua` — невалидный candidate не заменяет рабочий VM;
- `over-budget.lua` — бесконечный callback прерывается 8 ms budget hook;
- `texture-smoke.lua` — локальная WIC/D3D11 texture pipeline.

Проверенный сценарий:

1. Запустить `smoke.lua` с autoload и убедиться, что `ticks` растёт.
2. Заменить source на `invalid-hot-reload.lua`: процесс остаётся responsive, а
   `ticks` рабочего state продолжает расти.
3. Вернуть валидный source: `loads` увеличивается ровно на один.
4. Запустить `over-budget.lua`: процесс остаётся responsive, CPU не занят
   busy-loop, state отображается как `Over budget`.
5. Для texture fixture положить `ct.png` рядом со скриптом; storage должен
   содержать `texture_loaded=true`.
