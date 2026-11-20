# material symbols

the svg files are Material Symbols Rounded (weight 400, grade 0, 24 px, filled variants where the name says so) from
https://github.com/google/material-design-icons (commit 10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7, `symbols/web/<name>/materialsymbolsrounded`).
they are licensed under the Apache License 2.0, see `LICENSE-Apache-2.0.txt`. `incognito.svg` is drawn for yozora, nothing of the set has a mask like it.

| file | Material Symbols name |
| --- | --- |
| back, forward | arrow_back, arrow_forward |
| chevron | chevron_right |
| reload | refresh |
| close, menu | close, more_vert |
| lock, search | lock, search |
| star, star-filled | star, star with fill 1 |
| plus | add |
| bookmarks, history | bookmarks, history |
| shield, settings | shield, settings |
| play, pause | play_arrow, pause with fill 1 |
| download, copy, check, delete | download, content_copy, check, delete |
| tabs | tab_unselected |
| language, palette, text-size | language, palette, format_size |
| cookie, sweep, code | cookie, delete_sweep, code |
| speed, animation, font, info | speed, animation, font_download, info |
| share, tab-new, image, link | share, tab_new_right, image, link |
| hardware, bug-report, terminal | hardware, bug_report, terminal |
| receipt-long, bolt, memory, brush | receipt_long, bolt, memory, brush |

`tools/make_ui_icons.py` turns every file here into the 96 pixel alpha mask `app/icons/ui/yz-<file>.png`.
