# Proteção da branch principal (`main`)

## Objetivo

Impedir push direto e merges sem CI verde na branch de produção.

## Estado esperado

| Regra | Valor |
|-------|--------|
| Target | `refs/heads/main` |
| Pull request obrigatório | sim |
| Aprovações mínimas | 1 |
| Dismiss stale reviews | sim |
| Require conversation resolution | sim |
| Status checks | `security-scan`, `validate-secrets-template`, `unit-tests`, `firmware-build` |
| Require branches up to date | sim |
| Block force pushes | sim |
| Block deletions | sim |

## Aplicar via script

```bash
# No clone do repositório, com gh autenticado como admin:
./scripts/protect-main-branch.sh
```

Para outro repositório:

```bash
REPO=avilarezende/segportal BRANCH=main ./scripts/protect-main-branch.sh
```

## Aplicar via UI (GitHub)

1. Abra `https://github.com/<owner>/<repo>/settings/rules`
2. **New ruleset** → **New branch ruleset**
3. Nome: `protect-main`
4. Target branches: `main`
5. Marque:
   - Restrict deletions
   - Block force pushes
   - Require a pull request before merging (1 approval)
   - Require status checks to pass (adicione os 4 jobs do CI)
6. Enforcement: **Active** → Save

## Verificação

```bash
gh api repos/avilarezende/esp32/rulesets --jq '.[].name'
gh api repos/avilarezende/esp32/branches/main --jq '{name,protected}'
```
