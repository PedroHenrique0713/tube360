"""Teclas para navegar nas Configurações por seções (LB/RB trocam a seção, cima/baixo andam dentro dela).

Os ids são os índices históricos das linhas (0 conta, 1 vídeos por linha, 2 qualidade, 3 som, 4 sincronia, 5 idioma,
6 timer, 7 Shorts, 8 relatórios, 9 saída, 10 áudio, 11 texto grande, 12 encaixe, 13 caixas de som, 14 atualizações,
15 SponsorBlock, 16 apagar histórico, 17 apagar lista, 18 volume). Mantenha em sincronia com `settings_ui::sections()`
em client/src/ui_settings.hpp."""
SECTIONS = [[0], [2, 9, 12, 10, 15, 7], [3, 4, 13, 18], [5, 1, 11], [6], [8, 16, 17], [14]]


def _where(row):
    for s, rows in enumerate(SECTIONS):
        if row in rows:
            return s, rows.index(row)
    raise ValueError(row)


def seq(*steps, start=0):
    """Cada passo é um id de linha (vai até ela) ou um texto de teclas cru (ex.: 'RRR'). Começa na linha `start`."""
    keys, here = '', start
    for step in steps:
        if isinstance(step, str):
            keys += step
            continue
        s0, i0 = _where(here)
        s1, i1 = _where(step)
        if s1 != s0:
            keys += (']' if s1 > s0 else '[') * abs(s1 - s0)
            i0 = 0
        keys += ('D' if i1 > i0 else 'U') * abs(i1 - i0)
        here = step
    return keys


if __name__ == '__main__':
    assert seq(5) == ']]]' and seq(2) == ']' and seq(8) == ']]]]]' and seq(7) == ']DDDDD'
    assert seq(7, 'R', 8, 'R') == ']DDDDDR]]]]R' and seq(14) == ']]]]]]' and seq(18) == ']]D' * 0 + ']]DDD'
    print('settings_nav: ok')
