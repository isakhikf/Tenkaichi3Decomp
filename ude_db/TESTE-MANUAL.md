# Roteiro de controle físico — UDE-DB1

Este roteiro deve ser executado pelo usuário. Não há homologação nesta entrega.

Use uma pasta separada da C3, modo treino offline, oponente CPU nível 29 e jogador humano no lado 0. Mantenha `ude_enabled=0`. A DB1 começa desligada; F6 alterna `ude_db_enabled` e o indicador mostra `DB1 ON`. F7 pertence ao motor antigo e deve permanecer desligado nesta comparação.

Ative `ude_db_log=1` antes de iniciar o executável para gravar `ude_db_log.jsonl`. O arquivo cresce enquanto há combate; feche normalmente para descarregar o buffer. Não compare FPS com o log ligado. Não altere o tick nativo nem os dados do jogo.

Para cada linha abaixo, faça dez tentativas e preencha `tentativas-manuais.csv`. Anote personagem, golpe, distância, sucesso/falha, eventual interferência no controle e frame do log. Uma animação de guarda ou uma receita escolhida não comprova que a defesa foi aceita: confira a reação nativa e o dano. Técnicas excluídas são testes de ausência de automação.

| Técnica | Preparação / observação esperada |
|---|---|
| GUARD_HOLD | Ataques frontais não esquiváveis ou hits posteriores. Guarda mantida; ausência de bordas repetidas. Chip permitido pelo jogo não deve ser apagado. |
| GUARD_HIGH_LOW | Smashes direcionais com guard kinds distintos. Verificar altura escolhida e os três frames necessários para trocar uma pose já ativa. |
| VANISH_EVADE | Primeiro hit com flag 0x1; janela no piso. Ao sair de guarda segurada, deve haver liberação antes do toque. Toque não deve repetir dentro de 30 updates. |
| Z_COUNTER | Somente com `ude_db_z=1`. UP+RUSH frescos no contato; frame anterior deve liberar UP/RUSH se já estavam presentes. Retornar a 0 depois do teste. |
| SONIC_SWAY | Personagem com caps & 0x80 contra rush combo ID < 0xB, frontal. Observar os primeiros dez frames da postura e o dreno de ki do defensor. |
| VANISHING_STEP | Excluído da seleção automática desta candidata. Fazer manualmente e conferir ausência de injeção DB1 durante input/ação manual. |
| VANISH_BEHIND | Excluído. Não pode surgir receita automática para gastar AmountB. |
| BLAST_EVADE | Técnica/beam no primeiro contato, máscara 0xB1. Em hits posteriores ou contagem desconhecida, a DB1 pode recorrer à guarda frontal. |
| RUSH_EVADE | Rush-dash sem grab A5; conferir contato e reação nativa. Retorno zero do resolvedor isoladamente não comprova falha nem sucesso. |
| THROW_BREAK | Throw 0x55 frontal; rush grab A5 usa a janela 107C. Repetir por trás como caso negativo. |
| KI_DEFLECT | Caps & 0x20 e projétil redirecionável frontal. Verificar postura 3B; em personagens de absorção, a entrada nativa pode seguir 3D. Conferir renovação da borda depois que a postura termina. |
| PERFECT_DEFLECT | Flag 0x41 efetiva, janela 1074 no piso e param_flags & 2 == 0. Conferir flag nativa 0x6B; não inferir pela animação. |
| REFLECT_ABSORB | Sem receita autônoma própria. Características nativas do personagem podem produzir esses efeitos a partir da entrada legal de defesa; não contar toda absorção como deflect. |
| SIDESTEP | Somente com `ude_db_sidestep=1`, lock-on e proximidade. Retornar a 0 depois do teste. |
| DRAGON_DASH_ESCAPE | Excluído: CHARGE excederia o conjunto de bits aditivos autorizado. Não interpretar `ude_db_dragon_escape` como funcional. |
| EMERGENCY_BLASTER_WAVE | Excluído; nenhum duplo LOCKON automático ou gasto automático de vida. |
| ARMOR_REPEL | Passivo nativo; não deve ser atribuído a uma receita automática DB1. |
| SKILL_DEFENSE | Excluído; nenhum CHARGE+GUARD automático ou gasto automático de blast stock. |
| RECOVERY | Somente com `ude_db_recover=1`, blow-away D5 após frame 8 e capacidade nativa. Retornar a 0 depois do teste. |
| CLASH_MASH | Excluído; girar/martelar é responsabilidade do usuário. |
| CLASH_PROMPT | Excluído; o usuário responde ao botão pedido. |

Em cada família automática, repita pelo menos uma tentativa segurando direção e outra iniciando um ataque manual. A DB1 deve devolver o input físico intacto e permanecer fora durante a graça manual, a ação iniciada e um pedido pendente. O teste não exige provocar uma conexão online: o bloqueio online/replay/CPU é coberto pelos testes do adaptador e do núcleo.

Para reproduzir as decisões de um log completo:

```text
python port/tests/ude_db/replay_logs.py --db1-log CAMINHO/ude_db_log.jsonl
```

No Windows sem gcc/clang em PATH, acrescente `--zig CAMINHO/zig.exe`. Os logs C5/C3 antigos não contêm todo o snapshot DB1; não permitem calcular sucessos contrafactuais desta candidata. A flag 0x7B não está exposta pelo leitor legado de resultados; o Z vem desligado por padrão e sua aceitação deve ser conferida no teste físico/registro nativo apropriado.
