# AGENTS.md

Guia para agentes que trabalham neste repositório (`esp32`).

## Projeto

Firmware ESP32 em ESP-IDF v5.3.2. No boot conecta ao Wi-Fi salvo no NVS; sem
credenciais (ou falha), sobe o portal de configuração `ESP32-Setup` (SoftAP +
página web). Veja o `README.md` para o fluxo completo, build e emulação.

Comandos essenciais (após `. "$HOME/esp/esp-idf/export.sh"`):

- Build hardware: `idf.py build`
- Build QEMU (rádio desligado, portal via Ethernet emulada):
  `idf.py -B build_qemu -DSDKCONFIG=build_qemu/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" build`
- Emular: `idf.py -B build_qemu qemu` (portal em `openeth`; use `hostfwd=tcp::8080-:80` para acessar via `http://127.0.0.1:8080/`).

## Roteamento de modelos (Cursor + OpenCode Go)

Preferência GLOBAL do autor deste projeto: **usar OpenCode Go de forma
consistente**. Esta seção registra a diretriz no repositório; ela orienta as
escolhas do agente e a herança de subagents, mas **não altera o roteador do
"Auto" do Cursor** (ver limitações abaixo).

- Padrão: preferir OpenCode Go como primeira opção sempre que qualidade/tool-use
  não for prioridade explícita.
- Auto: quando em Auto, considerar os modelos do OpenCode Go nas tarefas MENOS
  complexas (edição rápida, boilerplate, perguntas curtas, correções pequenas,
  single-file). Reservar infra Cursor forte (Composer/Claude) só para tarefas
  realmente complexas.

Escolha de id Go:

- Edição rápida / boilerplate / perguntas curtas: `deepseek-v4-flash` ou `qwen3.8-flash`
- Coding agent barato e forte: `kimi-k2.7-code`
- Raciocínio / refactor maior: `deepseek-v4-pro` ou `minimax-m2.7`
- Visão / imagens: `deepseek-v4-flash-vision-exp` ou `mimo-v2-omni`
- Evitar ids que colidem com o catálogo Cursor: `glm-5.2`, `kimi-k3`.

Subagents (Task) — permitir Go independentemente da herança:

- Se `available_subagent_models` contiver um id do OpenCode Go, criar o subagent
  DIRETO com esse id Go (sem depender de herança), escolhendo pela complexidade
  acima.
- Se NÃO houver id Go em `available_subagent_models`, herdar (`inherit`) de um
  pai já colocado em Go; alternativa `composer-2.5-fast`.
- Nunca inventar um id Go que não esteja em `available_subagent_models` (a
  chamada falha).

Limitações atuais do produto (não contornáveis por regra):

- O "Auto" do Cursor e os Cloud Agents não roteiam para o OpenCode Go
  automaticamente. Para garantir Go, selecione um id Go no picker de modelos.
- Subagents só aceitam ids presentes em `available_subagent_models`; hoje essa
  lista normalmente não inclui ids do OpenCode Go, então o caminho efetivo é a
  herança a partir de um pai em Go.

Observação: esta é uma preferência global do autor. A fonte de verdade para uso
em todos os workspaces são as **User Rules** do Cursor (Settings → Rules); este
arquivo apenas versiona a diretriz para quem abrir o projeto.
