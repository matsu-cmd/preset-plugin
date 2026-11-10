-- =============================================================
-- demo.sql — демонстрация работы базы данных пресетов
-- Запуск:  psql $PRESET_DB_URL -f scripts/demo.sql
-- =============================================================

\echo ''
\echo '============================================================'
\echo ' 1. СТРУКТУРА БАЗЫ ДАННЫХ'
\echo '============================================================'
\echo ''
\echo '--- Все таблицы ---'
\dt

\echo ''
\echo '--- Структура таблицы presets ---'
\d presets

\echo ''
\echo '--- Структура таблицы preset_history ---'
\d preset_history

\echo ''
\echo '============================================================'
\echo ' 2. СОДЕРЖИМОЕ ТАБЛИЦ'
\echo '============================================================'
\echo ''
\echo '--- Плагины ---'
SELECT id, name, vendor, version FROM plugins ORDER BY name;

\echo ''
\echo '--- Категории ---'
SELECT id, name FROM categories ORDER BY name;

\echo ''
\echo '--- Теги ---'
SELECT id, name FROM tags ORDER BY name;

\echo ''
\echo '--- Пресеты (без JSONB-параметров) ---'
SELECT id, name, category_id, is_factory, is_public
FROM presets ORDER BY name;

\echo ''
\echo '--- Пресеты с параметрами ---'
SELECT name, params FROM presets ORDER BY name;

\echo ''
\echo '============================================================'
\echo ' 3. СВЯЗЬ M:N (ПРЕСЕТЫ <-> ТЕГИ)'
\echo '============================================================'
\echo ''
SELECT p.name AS preset, array_agg(t.name ORDER BY t.name) AS tags
FROM presets p
LEFT JOIN preset_tags pt ON pt.preset_id = p.id
LEFT JOIN tags t ON t.id = pt.tag_id
GROUP BY p.id, p.name
ORDER BY p.name;

\echo ''
\echo '============================================================'
\echo ' 4. ИНДЕКСЫ'
\echo '============================================================'
\echo ''
SELECT indexname, indexdef
FROM pg_indexes
WHERE tablename IN ('presets', 'preset_tags', 'preset_history')
ORDER BY tablename, indexname;

\echo ''
\echo '============================================================'
\echo ' 5. ТРИГГЕРЫ'
\echo '============================================================'
\echo ''
SELECT tgname AS trigger_name,
       tgrelid::regclass AS table_name,
       CASE tgtype & 2 WHEN 2 THEN 'BEFORE' ELSE 'AFTER' END AS timing,
       CASE
         WHEN tgtype & 4 = 4 THEN 'INSERT'
         WHEN tgtype & 8 = 8 THEN 'DELETE'
         WHEN tgtype & 16 = 16 THEN 'UPDATE'
         ELSE 'OTHER'
       END AS event
FROM pg_trigger
WHERE tgname LIKE 'presets_%'
ORDER BY tgname;

\echo ''
\echo '============================================================'
\echo ' 6. ДЕМОНСТРАЦИЯ ТРИГГЕРА АУДИТА'
\echo '============================================================'
\echo ''
\echo '--- Текущее значение gain у Warm Pad ---'
SELECT name, params->>'gain' AS gain
FROM presets WHERE name = 'Warm Pad';

\echo ''
\echo '--- Изменяем gain с 0.7 на 0.55 через UPDATE ---'
UPDATE presets
SET params = jsonb_set(params, '{gain}', '0.55')
WHERE name = 'Warm Pad';

\echo ''
\echo '--- История изменений (триггер должен был записать) ---'
SELECT
    preset_id,
    old_params->>'gain' AS old_gain,
    new_params->>'gain' AS new_gain,
    changed_at
FROM preset_history
ORDER BY changed_at DESC
LIMIT 5;

\echo ''
\echo '============================================================'
\echo ' 7. АНАЛИТИЧЕСКОЕ ПРЕДСТАВЛЕНИЕ'
\echo '============================================================'
\echo ''
\echo '--- v_preset_stats ---'
SELECT * FROM v_preset_stats;

\echo ''
\echo '============================================================'
\echo ' 8. ПОИСК ПО JSONB (GIN-ИНДЕКС)'
\echo '============================================================'
\echo ''
\echo '--- Найти пресеты, где gain >= 0.8 ---'
SELECT name, params->>'gain' AS gain
FROM presets
WHERE (params->>'gain')::numeric >= 0.8
ORDER BY gain DESC;

\echo ''
\echo '--- Найти пресеты с cutoff < 500 ---'
SELECT name, params->>'cutoff' AS cutoff
FROM presets
WHERE (params->>'cutoff')::numeric < 500
ORDER BY cutoff;

\echo ''
\echo '============================================================'
\echo ' 9. ПЛАН ЗАПРОСА (EXPLAIN ANALYZE)'
\echo '============================================================'
\echo ''
\echo '--- Поиск по params @> (должен использовать GIN) ---'
EXPLAIN (ANALYZE, BUFFERS, COSTS OFF)
SELECT name FROM presets WHERE params @> '{"mode": "lp"}';

\echo ''
\echo '--- Нечёткий поиск по имени (pg_trgm GIN) ---'
EXPLAIN (ANALYZE, COSTS OFF)
SELECT name FROM presets WHERE name ILIKE '%Pad%';

\echo ''
\echo '============================================================'
\echo ' ДЕМОНСТРАЦИЯ ЗАВЕРШЕНА'
\echo '============================================================'
\echo ''
