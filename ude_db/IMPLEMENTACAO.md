# UDE-DB1 — implementação isolada

Data: 2026-11-09. Branch: `ude-db1`, em cópia nova. Base oficial 0.1.39.6 (`48d66c1`) integrada à interface Hybrid v5 (`cb716e1`). A pasta C3 e as branches antigas permanecem preservadas.

| Fase | Resultado |
|---|---|
| 1 — auditoria | 21 técnicas, sete janelas e 26.243 registros. Campos comunitários/não encontrados continuam explicitamente fora da autorização de política. |
| 2 — geração | JSON → SQLite e header determinístico; drift detectado. As 30 tabelas prévias do SQLite copiado foram comparadas integralmente e permanecem iguais. |
| 3 — política | Núcleo escalar próprio, adaptador exclusivo, toggle F6/default 0, razões por receita. Não há escrita em flags, timers, posição, resultado de colisão ou recursos do jogo. |
| 4 — validação | Testes automatizados e replay descritos abaixo. Build CI e hashes finais ficam em `BUILD.json`. Testes físicos são do usuário. |

O JSON continua v1.1.0 e recebe `ude_db1_policy` com prioridades, pacotes, classes, janelas e defaults. O selo histórico `meta.auditoria.policy_ready=false` refere-se ao banco descritivo completo: há nomes/listas que não foram certificados. A candidata consome apenas o subconjunto numérico e os predicados documentados, sem transformar esses campos pendentes em autorizações.

## Seleção e entrada física

`UdeDb_Choose(snapshot)` retorna receita, frame de pressão, razão e motivo de recusa de cada outra receita. `UdeDb_Step` mantém somente memória própria de graça manual, ação iniciada, último toque e liberação. A ordem está no JSON e é compilada; uma receita válida de prioridade inferior recebe `lower_priority`.

O adaptador novo é chamado de `BtlInput_BuildRecord`, no lugar da chamada direta ao adaptador antigo. Com DB1 desligada, delega ao caminho antigo. Com DB1 ligada, não há fallthrough para esse caminho. Os dois motores começam desligados no pacote entregue. F6 controla a DB1; F7 conserva o toggle antigo.

O resultado é sempre `physical | added`. Direções e botões físicos cancelam a contribuição DB1 no mesmo frame. A graça manual e o pedido pendente também impedem injeção; uma ação manual em andamento mantém a posse até voltar a um estado neutro/reação. Online, replay, resimulação e lado CPU não recebem saída DB1; o adaptador recusa os primeiros casos antes de chamar getters do lutador/frame.

`BtlCharApi_SetInjectedInput` foi preservado. Acionar seu `injectOn` faria o jogo pular a coleta do controle físico. A DB1 usa o ponto já integrado de construção da palavra e a construção nativa dos comandos, sem ativar esse modo da CPU.

Uma guarda recém-iniciada também abre a borda GUARD; por isso o fallback GUARD_HOLD não pode contornar o cooldown das quatro janelas compartilhadas. Ao trocar guarda segurada por toque, o motor prepara liberação quando ainda há um update até o contato. Não declara um toque novo com GUARD já segurado. O Z-Counter de um update e a esquiva com penalidade de nível pressionam somente no contato. A postura de deflect renova sua borda quando termina, respeitando o intervalo mínimo.

## Leituras adicionais autorizadas

O usuário autorizou em 2026-11-09 duas leituras no caminho DB1: flag nativa 0x41 e `EftHit_GetHitCount` do projétil. `BtlHit_PortUdeDbQuery` só lê esses valores. O máximo de hits já recebidos no grupo de contatos a até 0,5 update do mais próximo evita classificar um contato simultâneo já atingido como primeiro hit. As receitas antigas chamam o modo sem essas leituras adicionais.

O índice da tabela de ataques é o character ID obtido do objeto oponente por `BtlCharApi_GetChara`, não o slot do membro ativo. Além dos limites, o núcleo exige coincidência dos flags do registro compilado com os flags vivos para receitas de melee. Desconhecido/divergente é recusado.

## Limites intencionais

- Skills, transformação, Emergency Blaster Wave, vanish behind, passivos e clashes não são receitas autônomas da DB1.
- Z, sidestep e recovery são opt-in e começam em 0.
- Dragon Dash Escape não é automatizado: CHARGE excederia o conjunto de bits aditivos autorizado. Seu registro continua no banco para documentação.
- A guarda pode sofrer chip ou não resolver um ataque pelas regras nativas. A DB1 não altera essas regras e não garante invulnerabilidade.
- A flag 0x7B não é exposta pela consulta de resultados legada; não foi acrescentada uma terceira leitura além das duas autorizadas. Z vem desligado. O replay registra flags observáveis e não inventa esse resultado.

## Validação reproduzível

| Checagem | Resultado local |
|---|---:|
| Política e arbitragem | 84 testes, zero falhas |
| Adaptador / fronteira de leituras | 14 testes, zero falhas |
| Funções C nativas e registros | 472.468 verificações, zero falhas |
| Temporização com função nativa | 3.000 updates, 84 toques, zero resets anti-mash causados por eles |
| Campanha de segurança sintética | 20.000 updates; zero sobrescritas físicas e zero intervalos de pressão menores que 30 |
| Replay DB1 de fixture sintética | 120/120 decisões e razões reproduzidas; intervalo mínimo 30 |
| Replay real C5 do motor antigo | 599/599 decisões de injeção; 597/599 razões e 588/599 classes iguais |
| Logs C3 históricos | 1.655.236 registros examinados em três arquivos |

As funções de timer e acesso a guard kinds usadas no teste são extraídas diretamente do fonte C, compiladas sem alterar seus corpos e comparadas com os valores do JSON. Isso valida semântica/extração, sem executar uma luta. O teste do adaptador não fornece funções mutadoras do jogo: uma dependência nova delas impediria o link.

Os registros C3 contêm sessões de configurações/épocas distintas, inclusive material online. Foram observadas 347.216 amostras de filtro de pad, 37.449 alterações de saída e 26.217 amostras que retiraram algum bit físico. Esses números descrevem o caminho C3 registrado, não uma regressão ou resultado da DB1. As 16.458 transições de janelas para -1 não podem ser atribuídas automaticamente à assistência: também há input manual e amostras repetidas.

Não há taxa de sucesso contrafactual C3 × DB1: os logs antigos não têm o snapshot completo, ETA antes do input e estado de posse/configuração necessários. O replayer aponta campos ausentes em vez de preencher hipóteses. O novo log DB1 inclui snapshot, configuração, estado anterior e recusas por receita; pode ser reproduzido integralmente depois do teste físico. Não foram iniciadas lutas automatizadas no computador do usuário enquanto havia jogo aberto.

Comandos na raiz da cópia:

```text
python tools/defense_db/build.py
python tools/defense_db/build.py --check
python port/tests/ude_db/check_generated.py
python port/tests/ude_db/run_tests.py
python port/tests/ude_db/replay_logs.py --c3-dir CAMINHO/diagnosticos-defesa
```

No Windows usado nesta sessão, as duas últimas chamadas receberam `--zig C:/Users/isakhi_/Documents/Codex/2026-10-23/cr/work/bt3-v3/compiler/zig-windows-x86_64-0.13.0/zig.exe`. O CI usa gcc e o workflow existente `ude-build.yml`, seguido de `port/release/inside.sh` para o Windows x64. O SQLite é gerado localmente e não precisa existir no jogo durante a luta.

O roteiro físico e as 210 linhas de registro estão em `TESTE-MANUAL.md` e `tentativas-manuais.csv`. Nada está homologado.
