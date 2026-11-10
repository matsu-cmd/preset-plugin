-- =============================================================
-- Preset Database Schema
-- =============================================================

CREATE EXTENSION IF NOT EXISTS pgcrypto;
CREATE EXTENSION IF NOT EXISTS pg_trgm;

-- ---------- Справочники ----------
CREATE TABLE IF NOT EXISTS users (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username    TEXT NOT NULL UNIQUE,
    email       TEXT NOT NULL UNIQUE CHECK (email ~* '^[^@]+@[^@]+\.[^@]+$'),
    role        TEXT NOT NULL DEFAULT 'user' CHECK (role IN ('user','admin')),
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS plugins (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name        TEXT NOT NULL UNIQUE,
    vendor      TEXT,
    version     TEXT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS categories (
    id          SERIAL PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    description TEXT
);

-- ---------- Метаданные параметров ----------
CREATE TABLE IF NOT EXISTS plugin_params (
    plugin_id   UUID NOT NULL REFERENCES plugins(id) ON DELETE CASCADE,
    param_key   TEXT NOT NULL,
    param_type  TEXT NOT NULL CHECK (param_type IN ('float','int','bool','string','enum')),
    min_value   NUMERIC,
    max_value   NUMERIC,
    default_val JSONB,
    PRIMARY KEY (plugin_id, param_key),
    CHECK (min_value IS NULL OR max_value IS NULL OR min_value <= max_value)
);

-- ---------- Теги ----------
CREATE TABLE IF NOT EXISTS tags (
    id   SERIAL PRIMARY KEY,
    name TEXT NOT NULL UNIQUE
);

-- ---------- Пресеты ----------
CREATE TABLE IF NOT EXISTS presets (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    plugin_id       UUID NOT NULL REFERENCES plugins(id) ON DELETE CASCADE,
    author_id       UUID REFERENCES users(id) ON DELETE SET NULL,
    category_id     INT  REFERENCES categories(id) ON DELETE SET NULL,
    name            TEXT NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    params          JSONB NOT NULL,
    schema_version  INT NOT NULL DEFAULT 1,
    is_factory      BOOLEAN NOT NULL DEFAULT FALSE,
    is_public       BOOLEAN NOT NULL DEFAULT FALSE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS preset_tags (
    preset_id UUID NOT NULL REFERENCES presets(id) ON DELETE CASCADE,
    tag_id    INT  NOT NULL REFERENCES tags(id)    ON DELETE CASCADE,
    PRIMARY KEY (preset_id, tag_id)
);

CREATE TABLE IF NOT EXISTS preset_history (
    id          BIGSERIAL PRIMARY KEY,
    preset_id   UUID NOT NULL REFERENCES presets(id) ON DELETE CASCADE,
    changed_by  UUID REFERENCES users(id),
    old_params  JSONB,
    new_params  JSONB,
    changed_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- ---------- Индексы ----------
CREATE INDEX IF NOT EXISTS idx_presets_plugin       ON presets(plugin_id);
CREATE INDEX IF NOT EXISTS idx_presets_author       ON presets(author_id);
CREATE INDEX IF NOT EXISTS idx_presets_category     ON presets(category_id);
CREATE INDEX IF NOT EXISTS idx_presets_public       ON presets(is_public) WHERE is_public;
CREATE INDEX IF NOT EXISTS idx_presets_name_trgm    ON presets USING GIN (name gin_trgm_ops);
CREATE INDEX IF NOT EXISTS idx_presets_params_gin   ON presets USING GIN (params jsonb_path_ops);
CREATE INDEX IF NOT EXISTS idx_preset_tags_tag      ON preset_tags(tag_id);
CREATE INDEX IF NOT EXISTS idx_history_preset_time  ON preset_history(preset_id, changed_at DESC);

-- уникальность имени пресета в рамках плагина и автора (регистронезависимо)
CREATE UNIQUE INDEX IF NOT EXISTS presets_unique_name
    ON presets (plugin_id, author_id, lower(name));

-- ---------- Триггеры ----------
CREATE OR REPLACE FUNCTION trg_touch_updated_at() RETURNS trigger AS $$
BEGIN
    NEW.updated_at := now();
    RETURN NEW;
END $$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS presets_touch ON presets;
CREATE TRIGGER presets_touch
BEFORE UPDATE ON presets
FOR EACH ROW EXECUTE FUNCTION trg_touch_updated_at();

CREATE OR REPLACE FUNCTION trg_log_preset_change() RETURNS trigger AS $$
BEGIN
    IF TG_OP = 'UPDATE' AND OLD.params IS DISTINCT FROM NEW.params THEN
        INSERT INTO preset_history(preset_id, old_params, new_params)
        VALUES (OLD.id, OLD.params, NEW.params);
    END IF;
    RETURN NEW;
END $$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS presets_audit ON presets;
CREATE TRIGGER presets_audit
AFTER UPDATE ON presets
FOR EACH ROW EXECUTE FUNCTION trg_log_preset_change();

-- ---------- Представления ----------
CREATE OR REPLACE VIEW v_preset_stats AS
SELECT pl.name AS plugin, c.name AS category, count(*) AS cnt
FROM presets p
JOIN plugins pl ON pl.id = p.plugin_id
LEFT JOIN categories c ON c.id = p.category_id
GROUP BY pl.name, c.name;
