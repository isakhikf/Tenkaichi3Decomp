# UDE-DB1 — relatório da continuação

Data: 2026-11-09. Branch separada: `ude-db1`. Base oficial: 0.1.39.6 (`48d66c1`), com a interface Hybrid v5 preservada.

| Fase | Situação |
|---|---|
| Auditoria | 21 técnicas, sete janelas e 26.243 registros; pendências por campo continuam explícitas. |
| Gerador | JSON → header/SQLite; check de drift passou; 30 tabelas prévias preservadas integralmente. |
| Política | Implementada, escalar, sem alocação ou headers do jogo; toggle próprio e default desligado. |
| Testes automatizados | Passaram; números abaixo. |
| Windows x64 | **Build e verificações passaram** no run 42592500592; hashes em `BUILD.json`. |
| Controle físico / eficácia C3 × DB1 | Pendente do roteiro do usuário e de novos logs completos. Não homologado. |

| Verificação | Resultado |
|---|---:|
| Casos de política e arbitragem | 84, zero falhas |
| Casos do adaptador | 14, zero falhas |
| Verificações contra funções C nativas e tabelas | 472.468, zero falhas |
| Temporização pela função nativa | 3.000 updates; 84 toques; zero resets anti-mash causados por eles |
| Segurança aleatória | 20.000 updates; zero sobrescritas físicas e zero intervalos de pressão menores que 30 |
| Replay de fixture DB1 | 120/120 decisões e razões; intervalo mínimo 30 |
| Replay real C5 pelo motor antigo | 599/599 decisões de injeção; 597/599 razões; 588/599 classes |
| Registros históricos C3 examinados | 1.655.236 em três arquivos |
| Invariantes de fonte nativo/legado | 17 comparações exatas, todas iguais |
| Arquivos congelados C3 conferidos | 77, nenhum alterado |

As verificações nativas compilam os corpos originais dos timers e acessores e comparam seus resultados aos dados extraídos. São verificações de implementação, sem simular aceitação de defesa numa luta completa. O build mantém as funções nativas de defesa e o tick do gerenciador de luta; não há alegação de FPS medido.

A política trata a borda de entrada explicitamente: libera antes de converter guarda segurada em toque, exige UP+RUSH novos no Z e DASH+direção novos no sidestep. Pausas e toggles preservam o histórico de pressão e a posse manual. O Z de um update e a esquiva com penalidade de nível pressionam no contato.

Foram acrescentadas, com autorização do usuário, somente duas leituras de elegibilidade no caminho DB1: flag nativa 0x41 e contagem de hits do projétil. O índice dos ataques vem do character ID do objeto oponente, não do slot do membro ativo. Flags vivos divergentes do registro compilado impedem receitas de melee.

Os logs C3 contêm configurações/épocas distintas e material online. As aceitações nativas observadas podem incluir input manual e efeitos legados; não são prova de automação DB1. Também não é possível atribuir automaticamente cada transição para -1 à assistência. Por isso a comparação contrafactual de sucessos permanece sem valor numérico. O replayer informa os campos ausentes; o novo log DB1 inclui snapshot, configuração, estado anterior e recusas por receita.

A flag 0x7B não é exposta pelo leitor legado de resultados. Não foi acrescentada uma terceira leitura além das duas autorizadas; Z está desligado por padrão. Não foram iniciadas lutas automatizadas no computador do usuário enquanto havia jogo aberto.

Os comandos executados e as evidências por arquivo estão em `IMPLEMENTACAO.md`, `AUDIT.md`, `CHANGELOG.md` e `resultados/`. O build usa o workflow existente `ude-build.yml` e o processo oficial `port/release/inside.sh`. Os executáveis antigos, os logs e as branches `ude-integration`, `ude-v4` e `ude-hybrid-v5` foram preservados.

Build verificado: [ude-build 42592500592](https://github.com/isakhikf/Tenkaichi3Decomp/actions/runs/42592500592), commit `96cac4b26f6c9bf11d52b9517629b121e32c5cb1`.
