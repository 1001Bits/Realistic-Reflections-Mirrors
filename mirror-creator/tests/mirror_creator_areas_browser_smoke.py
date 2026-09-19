"""Real Shift-click, source-ID, gap, rejection and download browser checks."""
import argparse
import io
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'tests'))
from test_mirror_creator_areas import broken_fixture, separated_parts
from test_mirror_creator_export import metadata
import mirror_creator_export as export


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    from playwright.sync_api import sync_playwright,expect
    from PIL import Image
    checks=[];errors=[];external=[]
    def passed(name):checks.append(name);print('PASS',name,flush=True)
    with sync_playwright() as p:
        browser=p.chromium.launch(executable_path='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
                                 headless=True,args=['--use-angle=swiftshader','--enable-unsafe-swiftshader'])
        page=browser.new_page(viewport=dict(width=1400,height=960),accept_downloads=True)
        page.set_default_timeout(15000)
        page.on('pageerror',lambda e:errors.append(str(e)))
        page.on('request',lambda r:external.append(r.url) if not r.url.startswith((args.url,'data:','blob:')) else None)
        page.goto(args.url,wait_until='networkidle')
        def idle():expect(page.locator('#viewport')).to_have_attribute('aria-busy','false')
        def upload(raw,name):
            with page.expect_response('**/api/import?*') as response:
                page.locator('#nif-input').set_input_files(dict(name=name,mimeType='application/octet-stream',buffer=raw))
            assert response.value.ok,response.value.text()
            idle();expect(page.locator('#save')).to_be_disabled()
        def click(x,shift=False,ok=True):
            box=page.locator('#canvas').bounding_box()
            if shift:page.keyboard.down('Shift')
            try:
                with page.expect_response('**/api/select') as response:
                    page.mouse.click(box['x']+box['width']*x,box['y']+box['height']*.5)
            finally:
                if shift:page.keyboard.up('Shift')
            idle();assert response.value.ok==ok,response.value.text()
            return response.value.json()
        def save(name):
            with page.expect_download() as download:page.locator('#save').click()
            assert not download.value.failure()
            out=args.output/name;download.value.save_as(str(out));idle();return out.read_bytes()
        def quiet():
            expect(page.locator('#message')).to_be_hidden()
            assert ' '.join(page.locator('.toolbar').inner_text().split())=='Select NIF Save hold shift to select multiple parts'

        broken=broken_fixture()
        (args.output/'broken-mirror-shift-test.nif').write_bytes(broken)
        for label,profile in (('prepared pane',1),):
            upload(broken,'broken-mirror.nif')
            assert click(.38)['triangleCount']==2
            selected=click(.62,True);assert selected['triangleCount']==4
            quiet();page.mouse.move(10,100)
            screenshot=page.screenshot();shot=Image.open(io.BytesIO(screenshot)).convert('RGB')
            # The centre is the missing glass. A union bounding rectangle would
            # wrongly colour it; both real shards must remain highlighted.
            for x,expected in ((540,True),(700,False),(860,True)):
                r,g,b=shot.getpixel((x,524));assert (g>r+10 and g>b+10)==expected,(x,(r,g,b))
            (args.output/f'{profile}-broken-selected.png').write_bytes(screenshot)
            output=save(f'{profile}-broken-reflective.nif');_,_,tags=metadata(output)
            assert tags[export.PROFILE_NAME]==profile and len(tags[export.TRIANGLES_NAME])==24
            passed(label+': Shift-click joins separated shards, keeps the gap and saves one pane')
            assert click(.62,True)['triangleCount']==2
            assert click(.38,True) is None
            expect(page.locator('#save')).to_be_disabled()
            assert click(.38,True)['triangleCount']==2
            assert click(.62,True)['triangleCount']==4
            assert click(.62)['triangleCount']==2
            passed(label+': Shift toggles areas and clears the last; ordinary click replaces selection')
            upload(output,'saved-broken.nif')
            assert click(.38)['triangleCount']==2
            assert click(.62,True)['triangleCount']==4
            reopened=save(f'{profile}-broken-reopened.nif');_,_,tags=metadata(reopened)
            assert len(tags[export.TRIANGLES_NAME])==24
            passed(label+': saved broken pane reopens and its shards remain selectable')

        raw,_=separated_parts(reverse=True)
        (args.output/'separate-parts-shift-test.nif').write_bytes(raw)
        upload(raw,'separate-parts.nif')
        first=click(.335);second=click(.665,True)
        assert len(second['areas'])==2 and second['triangleCount']==4
        assert {r['flip'] for r in second['areas']}=={False,True}
        output=save('separate-parts-reflective.nif');_,_,tags=metadata(output)
        assert len(tags[export.TRIANGLES_NAME])==24
        page.mouse.move(10,100);page.screenshot(path=str(args.output/'separate-parts-selected.png'))
        passed('Separate NIF parts with different transforms and winding share one selected front')

        for kind,kwargs in (('offset',dict(offset=1)),('tilted',dict(tilted=True))):
            raw,_=separated_parts(**kwargs);upload(raw,kind+'.nif')
            assert click(.335)['triangleCount']==2
            before=save(kind+'-before.nif')
            error=click(.665,True,False)
            assert 'separate reflections' in error['error']
            expect(page.locator('#message')).to_be_visible();expect(page.locator('#save')).to_be_enabled()
            after=save(kind+'-after.nif');assert after==before
            quiet();passed(kind+': incompatible addition reports an error and preserves the exact savable selection')
        assert not errors,errors
        assert not external,external
        browser.close()
    (args.output/'verification.json').write_text(json.dumps(dict(passed=len(checks),checks=checks,
        errors=errors,externalRequests=external,browser='Edge / headless SwiftShader',gameLaunched=False),indent=2)+'\n')


if __name__=='__main__':main()
