# UDE-DB1 — Correção 1

A DB1 anterior podia pressionar GUARD_HOLD enquanto uma esquiva elegível ainda aguardava o contato. Isso abria cedo as janelas compartilhadas do jogo e podia deixá-las expiradas quando o golpe chegava. A nova decisão reserva a borda para a defesa temporizada e reavalia a elegibilidade a cada update. Input físico, prioridade manual, bloqueios offline e intervalo mínimo de pressão continuam valendo.

A reprodução local do build anterior registrou 1.886 snapshots, 123 amostras de contribuição e 11 bordas automáticas, com zero bits físicos removidos. Portanto havia emissão de input; o problema identificado envolve a seleção/temporização. O frame 844 está documentado em `diagnostico/reproducao-anterior.json`.

| Verificação da correção | Resultado |
|---|---:|
| Testes de política | 89, zero falhas |
| Adaptador | 14, zero falhas |
| Corpos nativos/tabelas/temporização | 472.480, zero falhas |
| Novas regressões no código anterior | Três falhas reproduzidas |
| Janelas no contato após preparação de seis updates | Abertas com vida alta e baixa |
| Replay sintético | 120/120 decisões e razões |
| Replay C5 legado | 599/599 decisões de injeção |

Não houve alteração dos timers, da velocidade, de dados do disco ou da C3. Foram alterados o núcleo escalar DB1 e seus testes. Não há leitura nativa adicional. O motivo em repouso também foi corrigido para `no_threat`.

Build Windows e conferência no jogo: ver `BUILD.json` e `VALIDACAO-NO-JOGO.json`. Uma reprodução de diagnóstico não homologa todas as técnicas e não estabelece uma taxa de eficácia. O teste físico final continua com o usuário.

Comandos executados na cópia:

```text
python port/tests/ude_db/run_tests.py --zig CAMINHO/zig.exe --out ../db1-regression-before
python port/tests/ude_db/run_tests.py --zig CAMINHO/zig.exe --out ../db1-regression-after
python port/tests/ude_db/replay_logs.py --zig CAMINHO/zig.exe --out ../db1-fix-replay
git push origin ude-db1
```

No pacote distribuído as assistências continuam desligadas por padrão; F6 liga a DB1. Z-Counter, sidestep e recovery continuam opcionais, desligados por padrão. Na instalação local já usada, a escolha salva de ativar a DB1 deve ser preservada. Para diagnosticar outra tentativa, use `ude_db_log=1` e feche o jogo normalmente para terminar o registro.

Conferência do executável corrigido no jogo: 2.401 snapshots, 6 bordas automáticas, intervalo mínimo 36 updates, zero amostras de input físico durante a luta e zero bits físicos removidos. Houve 3 amostras com flag nativa 0x12C. Esses sinais confirmam execução/aceitação nessa reprodução; não são uma taxa de sucesso nem comparação causal com a versão anterior.
