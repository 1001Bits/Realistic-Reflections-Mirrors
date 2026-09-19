"""Real 3D clicks verify CK identification, selection lifetime and safe labels."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'tests'))
import nif_block_edit as nif
from test_mirror_creator import fixture
from test_mirror_creator_areas import broken_fixture, separated_parts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--vanilla-nif', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    from playwright.sync_api import sync_playwright, expect
    checks, errors = [], []
    with sync_playwright() as p:
        browser = p.chromium.launch(
            executable_path='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
            headless=True, args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
        page = browser.new_page(viewport={'width': 1440, 'height': 1000})
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.goto(args.url, wait_until='networkidle')
        panel, rows = page.locator('#ck-selection'), page.locator('.ck-part')

        def idle():
            expect(page.locator('#viewport')).to_have_attribute('aria-busy', 'false')

        def upload(raw, name='source.nif'):
            with page.expect_response('**/api/import?*') as response:
                page.locator('#nif-input').set_input_files(
                    {'name': name, 'mimeType': 'application/octet-stream', 'buffer': raw})
            idle()
            return response.value

        def click(x=.5, y=.5, shift=False):
            box = page.locator('#canvas').bounding_box()
            if shift:
                page.keyboard.down('Shift')
            try:
                with page.expect_response('**/api/select') as response:
                    page.mouse.click(box['x']+box['width']*x, box['y']+box['height']*y)
            finally:
                if shift:
                    page.keyboard.up('Shift')
            idle()
            assert response.value.ok, response.value.text()
            return response.value.json()

        def passed(name):
            checks.append(name)
            print('PASS', name, flush=True)

        expect(panel).to_be_hidden()
        upload(fixture())
        expect(panel).to_be_hidden()
        click()
        expect(rows).to_have_text(['CK part: Pane · 3D Index 0'])
        expect(page.locator('#ck-note')).to_be_hidden()
        click(shift=True)
        expect(panel).to_be_hidden()
        passed('Whole-part click shows exact identity; removing the last area clears it')

        upload(broken_fixture())
        click(.38)
        expect(page.locator('#ck-note')).to_contain_text('CK changes the whole part')
        click(.62, shift=True)
        expect(rows).to_have_count(1)
        raw, _ = separated_parts()
        upload(raw)
        expect(panel).to_be_hidden()
        click(.335)
        click(.665, shift=True)
        expect(rows).to_have_text(['CK part: Pane · 3D Index 0', 'CK part: Pane · 3D Index 1'])
        expect(page.locator('#ck-note')).to_contain_text('one whole part')
        click(.335, shift=True)
        expect(rows).to_have_text(['CK part: Pane · 3D Index 1'])
        expect(page.locator('#ck-note')).to_be_hidden()
        passed('Shift selection deduplicates one part and distinguishes duplicate names by index')

        assert not upload(b'broken').ok
        expect(rows).to_have_text(['CK part: Pane · 3D Index 1'])
        upload(fixture())
        expect(panel).to_be_hidden()
        passed('Failed imports retain current identity; successful imports clear stale identity')

        model = nif.parse(fixture())
        name = '<img src=x onerror=alert(1)> ' + 'Long pane name '*16 + ':93'
        model.strings[1] = name
        upload(nif.serialize(model))
        click()
        expect(rows.locator('code')).to_have_text(name)
        assert rows.locator('img').count() == 0
        page.set_viewport_size({'width': 390, 'height': 780})
        box = panel.bounding_box()
        assert box['x'] >= 0 and box['x']+box['width'] <= 390
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
        page.screenshot(path=str(args.output/'narrow-long-name.png'))
        passed('Names are literal selectable text and wrap within a narrow viewport')

        page.set_viewport_size({'width': 1440, 'height': 1000})
        if args.vanilla_nif:
            imported = upload(args.vanilla_nif.read_bytes(), args.vanilla_nif.name).json()
            pane = next(m for m in imported['document']['meshes'] if m['ckName'] == 'WHIntWoodWallWindow03:13')
            assert pane['ckIndex'] == 3 and pane['block'] == 16
            page.screenshot(path=str(args.output/'vanilla-loaded.png'))
            # The window occupies the middle of the model's initial front view.
            selected = click()
            assert selected['block'] == pane['block'], selected
            expect(rows).to_have_text(['CK part: WHIntWoodWallWindow03:13 · 3D Index 3'])
            page.mouse.move(10, 100)
            page.screenshot(path=str(args.output/'vanilla-ck-part.png'))
            passed('Vanilla window click matches the previously verified CK row and index 3')

        assert not errors, errors
        browser.close()
    (args.output/'verification.json').write_text(json.dumps({'checks': checks, 'errors': errors}, indent=2)+'\n')


if __name__ == '__main__':
    main()
