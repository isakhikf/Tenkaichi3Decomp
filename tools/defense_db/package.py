#!/usr/bin/env python3
"""Add candidate-only settings/docs to official release archives. No game data."""
from pathlib import Path
import zipfile

ROOT=Path(__file__).resolve().parents[2]
settings='''ude_enabled=0
ude_db_enabled=0
ude_db_player=0
ude_db_grace=4
ude_db_tap_lead=1
ude_db_recover=0
ude_db_sidestep=0
ude_db_z=0
ude_db_log=0
'''
guide='''UDE-DB1 — candidata offline / base 0.1.39.6

As duas assistências iniciam DESLIGADAS neste pacote separado.
F6 liga/desliga DB1 e salva a escolha. O indicador mostra DB1 ON/--.
F7 é o toggle antigo, preservado; mantenha ude_enabled=0 ao testar DB1.
Online, replay e lado CPU não recebem intervenções DB1. Input manual tem
prioridade, inclusive direção e ação que o jogador já iniciou.

Para usar dados extraídos que já existem, defina BT3_DATA para a pasta
gamedata da sua instalação. O setup também permite extrair seu disco.
Não substitua arquivos da C3 ou de outras candidatas.

Configurações em bt3_settings.txt:
ude_db_enabled=0/1, ude_db_player=0/1/2, ude_db_grace=4,
ude_db_tap_lead=1 (máximo efetivo: um update), ude_db_recover=0,
ude_db_sidestep=0, ude_db_z=0, ude_db_log=0.
Log opcional: ude_db_log.jsonl (inclui snapshot e motivo de cada recusa).

Dragon Dash Escape não é automatizado: seu CHARGE excede o conjunto de
bits aditivos autorizado. Vanish behind, skills e clashes também ficam fora.
O banco numérico e as tabelas compiladas não garantem defesa em luta.
Roteiro de controle físico: dez tentativas por técnica, com sucesso/falha/
interferência. Esta candidata não é homologada.
'''


def main():
    found=0
    for archive in (ROOT/'port/build').glob('Tenkaichi3Decomp-*-x64.zip'):
        with zipfile.ZipFile(archive,'a',compression=zipfile.ZIP_DEFLATED) as z:
            names=set(z.namelist())
            if 'Tenkaichi3Decomp/bt3_settings.txt' in names:
                raise ValueError('Candidate settings already packaged; refusing duplicate member')
            z.writestr('Tenkaichi3Decomp/bt3_settings.txt',settings)
            z.writestr('Tenkaichi3Decomp/UDE-DB1-LEIA.txt',guide.encode('utf-8'))
        found+=1
    if found!=2:raise ValueError(f'Expected Linux and Windows archives, got {found}')
    print('Candidate-only settings/docs added; DB1 and legacy disabled by default.')


if __name__=='__main__':main()
