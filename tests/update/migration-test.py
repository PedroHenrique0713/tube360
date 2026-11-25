#!/usr/bin/env python3
"""Reproduz transferências com código histórico real, TLS local e chaves descartáveis.

Única troca de rede: CONNECT_TO loopback e CA privada no momento do perform.
HTTPS, verificação de host/certificado, libcurl, limite de redirects, allowlist,
assinatura, Range e hash continuam sendo os de cada versão, sem reimplementar o worker.
"""
import argparse
from functools import partial
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import ssl
import subprocess
import sys
import tarfile
import threading
from urllib.parse import urlsplit

ROOT=Path(__file__).resolve().parents[2]
OLD='PedroHenrique0713'
ORG='revive360'
RAW='https://raw.githubusercontent.com/'
WEB='https://github.com/'
CDN='https://release-assets.githubusercontent.com/github-production-release-asset/1/app.xex'


def run(*args, **kw):
    return subprocess.run([str(a) for a in args],check=True,capture_output=True,**kw).stdout


def compile_probe(build,folder,ref,key):
    source=folder/'source';source.mkdir(parents=True,exist_ok=True)
    hashes={}
    if ref=='working':
        shutil.copytree(ROOT/'client/src',source/'client/src',dirs_exist_ok=True)
    else:
        archive=run('git','archive',ref,'client/src')
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(source,filter='data')
    for name in ('update/service.cpp','update/common.cpp','update/app.cpp','yt/https.cpp'):
        hashes[name]=hashlib.sha256((source/'client/src'/name).read_bytes()).hexdigest()
    (source/'client/src/update/public_key.hpp').write_text('#pragma once\nnamespace updates { inline constexpr const char* publicKeyHex="'+key+'"; }\n')
    generated=source/'generated';generated.mkdir(exist_ok=True)
    for name in ('roots.hpp','update-roots.hpp'):
        shutil.copyfile(build/'yt'/name,generated/name)
    default=(source/'client/src/update/rules-default.json').read_text()
    (generated/'rules-default.hpp').write_text('namespace updates { inline constexpr const char* defaultRules=R"RULES('+default+')RULES"; }\n')
    link=shlex.split((build/'CMakeFiles/tube360.dir/link.txt').read_text())
    # Só dependências: os cinco fontes abaixo são da versão auditada, sem objetos de outra versão.
    libs=[str(build/x) if not x.startswith(('/','-')) else x for x in link
          if (x.endswith('.a') or '.so' in x or x.startswith('-l'))
          and 'libtube360-yt.a' not in x and 'libtube360-update.a' not in x]
    flags=(build/'CMakeFiles/tube360-update.dir/flags.make').read_text()
    includes=shlex.split(flags.split('CXX_INCLUDES = ',1)[1].splitlines()[0])
    includes=['-I'+str(source/'client/src'),'-I'+str(generated),*includes,
              '-I'+str(ROOT/'client/third_party/monocypher'),
              '-I'+str(next((ROOT/'.work/src').glob('curl-xenon-*/include')))]
    objects=[]
    for name in ('update/common.cpp','update/app.cpp','update/rules.cpp','update/service.cpp','yt/https.cpp'):
        obj=folder/(name.replace('/','-')+'.o')
        extra=['-include',str(ROOT/'tests/update/migration-network.hpp')] if name=='yt/https.cpp' else []
        run(link[0],'-std=c++20','-O1','-DPICOJSON_USE_LOCALE=0',*includes,*extra,
            '-c',source/'client/src'/name,'-o',obj)
        objects.append(obj)
    binary=folder/'probe'
    run(link[0],'-std=c++20','-O1','-DPICOJSON_USE_LOCALE=0',*includes,
        ROOT/'tests/update/migration.cpp',*objects,*libs,'-o',binary)
    return binary,hashes


class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def do_GET(self):
        server=self.server
        url='https://'+self.headers['Host']+self.path
        server.seen.append(url)
        old='/'+OLD+'/tube360/'
        org='/'+ORG+'/tube360/'
        is_org=org in self.path
        host=self.headers['Host']
        scenario=server.scenario
        def reply(status,body=b'',location=None,range_header=None):
            self.send_response(status)
            if location:self.send_header('Location',location)
            if range_header:self.send_header('Content-Range',range_header)
            self.send_header('Content-Length',str(len(body)))
            self.end_headers();self.wfile.write(body)
        if scenario=='loop':return reply(server.code,location=url)
        if host=='raw.githubusercontent.com':
            if scenario in ('transfer','configured-transfer','download-transfer') and old in self.path:
                return reply(server.code,location=url.replace(old,org))
            if scenario=='reverse-transfer' and org in self.path:
                return reply(server.code,location=url.replace(org,old))
            if scenario in ('same-owner','transport') and self.path.endswith('/rules/rules.json'):
                return reply(server.code,location=url.replace('/rules/rules.json','/mirror/rules.json'))
            if scenario in ('foreign','downgrade','userinfo','encoded') and self.path.endswith('rules.json'):
                target={'foreign':'https://evil.example/x','downgrade':'http://raw.githubusercontent.com/revive360/tube360/rules/rules.json',
                        'userinfo':'https://raw.githubusercontent.com@evil.example/x','encoded':RAW+ORG+'/tube360/%2e%2e/x'}[scenario]
                return reply(server.code,location=target)
            if is_org and scenario in ('fallback-404','fallback-bad-signature','fallback-bad-schema','split-pair','mixed'):
                if scenario=='fallback-404' or (scenario=='split-pair' and self.path.endswith('.sig')):
                    return reply(404)
                if scenario=='fallback-bad-signature' and self.path.endswith('.sig'):
                    return reply(200,b'0'*128+b'\n')
                if scenario=='fallback-bad-schema':return reply(200,server.documents['invalid'+('.sig' if self.path.endswith('.sig') else '')])
            name=self.path.rsplit('/',1)[-1]
            if scenario=='split-pair' and not is_org and not name.endswith('.sig'):return reply(404)
            if scenario=='mixed' and is_org and name.startswith('app'):return reply(404)
            if scenario=='mixed' and not is_org and name.startswith('rules'):return reply(404)
            if scenario=='all-bad-signature' and name.endswith('.sig'):return reply(200,b'0'*128+b'\n')
            if name.startswith('app'):
                manifest_owner=ORG if scenario in ('manifest-org','download-org') else OLD
                name='app-'+manifest_owner+('.sig' if name.endswith('.sig') else '')
            body=server.documents.get(name)
            return reply(200,body) if body else reply(404)
        if host=='github.com':
            if scenario in ('download-transfer','download-bad-host') and old in self.path:
                target=url.replace(old,org) if scenario=='download-transfer' else 'https://evil.example/payload'
                return reply(server.code,location=target)
            return reply(server.code,location=CDN)
        if host=='release-assets.githubusercontent.com':
            payload=server.payload
            if scenario=='download-bad-hash':payload=b'XEX2'+b'!'*(len(payload)-4)
            first,last=map(int,self.headers['Range'].removeprefix('bytes=').split('-'))
            value=f'bytes {first}-{last}/{len(payload)}'
            if scenario=='download-bad-range':value='bytes 0-1/2'
            return reply(206,payload[first:last+1],range_header=value)
        return reply(500,b'UNEXPECTED HOST')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,default=ROOT/'client/build')
    p.add_argument('--output',type=Path,default=ROOT/'.work/f50/migration')
    p.add_argument('--rc4',default='544c274')
    p.add_argument('--scenario',action='append',help='Executar somente este cenário (repetível)')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    key=out/'disposable.pem'
    if not key.exists():run(sys.executable,ROOT/'tools/sign-updates.py','--key',key,'keygen')
    public=run('openssl','pkey','-in',key,'-pubout','-outform','DER')[-32:].hex()
    cert=out/'tls.pem';tls_key=out/'tls-key.pem'
    run('openssl','req','-x509','-newkey','rsa:2048','-nodes','-subj','/CN=Tube360 migration local test',
        '-addext','subjectAltName=DNS:raw.githubusercontent.com,DNS:github.com,DNS:release-assets.githubusercontent.com,DNS:objects.githubusercontent.com,DNS:evil.example',
        '-days','2','-keyout',tls_key,'-out',cert)
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);server.daemon_threads=True
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,tls_key)
    server.socket=context.wrap_socket(server.socket,server_side=True)
    server.payload=b'XEX2'+b'fixture'*(180*1024)
    server.documents={}
    defaults=json.loads((ROOT/'client/src/update/rules-default.json').read_text());defaults['version']=2000
    def signed(name,data):
        path=out/name;path.write_bytes(data)
        run(sys.executable,ROOT/'tools/sign-updates.py','--key',key,'sign',path)
        return data,Path(str(path)+'.sig').read_bytes()
    for name,data in [('rules.json',defaults),('invalid',{'schema':'invalid'})]:
        raw,sig=signed(name,json.dumps(data).encode())
        server.documents[name]=raw;server.documents[name+'.sig']=sig
    for owner in (OLD,ORG):
        manifest=dict(schema=1,kind='tube360-app',build=6000,version='0.3.2-fixture',
            url=WEB+owner+'/tube360/releases/download/v0.3.2/tube360.xex',size=len(server.payload),
            sha256=hashlib.sha256(server.payload).hexdigest(),notes=dict(en='Local transfer fixture.',pt='Teste local de transferência.'))
        raw,sig=signed('app-'+owner,json.dumps(manifest).encode())
        server.documents['app-'+owner]=raw;server.documents['app-'+owner+'.sig']=sig
    env={**os.environ,'TUBE360_MIGRATION_CA':str(cert),'TUBE360_MIGRATION_PORT':str(server.server_port)}
    for name in ('XT_YT_REPLAY','XT_HTTP_DEBUG'):env.pop(name,None)
    # O curl do console compila sem suporte a proxy; limpar também no processo de prova.
    env={key:value for key,value in env.items() if not key.lower().endswith('_proxy')}
    results=[];source_hashes={}
    thread=threading.Thread(target=server.serve_forever);thread.start()
    try:
        for label,ref in [('v0.3.0','v0.3.0'),('v0.3.1','v0.3.1'),('rc4',a.rc4),('final','working')]:
            folder=out/label;folder.mkdir(exist_ok=True)
            binary,source_hashes[label]=compile_probe(a.build.resolve(),folder,ref,public)
            scenarios=['transport','same-owner','transfer','manifest-org','loop','foreign','downgrade','userinfo','encoded',
                       'download-transfer','download-cdn','download-org','download-legacy-layout','download-bad-host','download-bad-hash','download-bad-range']
            if label=='final':scenarios+=['configured-transfer','reverse-transfer','fallback-404','fallback-bad-signature','fallback-bad-schema','split-pair','mixed','all-bad-signature']
            if a.scenario:
                assert set(a.scenario)<=set(scenarios),a.scenario
                scenarios=[scenario for scenario in scenarios if scenario in a.scenario]
            for code in (301,302,307,308):
                for scenario in scenarios:
                    target=folder/f'{scenario}-{code}';target.mkdir(exist_ok=True)
                    # Repete sem usar cache de uma execução anterior.
                    for file in target.iterdir():file.unlink()
                    (target/'tube360.xex').write_bytes(b'XEX2original')
                    if scenario=='download-legacy-layout':(target/'tube360.xex').unlink()
                    if scenario=='configured-transfer':
                        (target/'updates-url.txt').write_text(RAW+OLD+'/tube360/rules/\n')
                    server.scenario=scenario;server.code=code;server.seen=[]
                    mode='transport' if scenario=='transport' else ('download' if scenario.startswith('download') else 'check')
                    if mode=='check' and (scenario in ('loop','split-pair','all-bad-signature') or
                        (scenario in ('transfer','manifest-org') and label!='final')):mode='reject'
                    if scenario=='download-transfer' and label!='final':mode='reject'
                    if scenario=='download-org' and label!='final':mode='reject'
                    actual=json.loads(run(binary,target,mode,env=env,timeout=15).decode().splitlines()[-1])
                    seen=list(server.seen)
                    if scenario=='transport':
                        assert actual=={'status':code,'error':''},actual
                        assert len(seen)==1,seen # O libcurl real não segue automaticamente.
                    else:
                        final=label=='final'
                        rule_ok=scenario not in ('loop','foreign','downgrade','userinfo','encoded','split-pair','all-bad-signature') and (scenario not in ('transfer','download-transfer') or final)
                        offered=scenario not in ('loop','split-pair','all-bad-signature') and (scenario not in ('transfer','manifest-org') or final)
                        if scenario.startswith('download'):
                            ready=scenario=='download-cdn' or (scenario in ('download-transfer','download-org') and final)
                            assert actual['ready']==ready,(label,scenario,actual,seen)
                            if scenario=='download-legacy-layout':
                                assert actual['installed']=='' and actual['error']=='setup',actual
                            else:assert actual['installed']=='XEX2original',actual
                            assert not actual['partial'],actual
                        else:assert actual['available']==offered,(label,scenario,actual,seen)
                        assert (actual['rules']==2000)==rule_ok,(label,scenario,actual,seen)
                        assert all(urlsplit(u).hostname!='evil.example' and u.startswith('https://') for u in seen),seen
                        if not final and scenario in ('transfer','download-transfer'):
                            assert not any('/revive360/' in u for u in seen),seen
                        if final and scenario=='transfer':
                            assert all('/revive360/' in u for u in seen),seen # A organização é a primeira origem.
                        if final and scenario.startswith('fallback'):
                            assert '/revive360/' in seen[0] and any('/PedroHenrique0713/' in u for u in seen),seen
                    results.append(dict(version=label,scenario=scenario,redirect=code,result=actual,requests=seen))
            print(f'PASS {label}: real HTTPS/worker, all 301/302/307/308 scenarios',flush=True)
    finally:
        server.shutdown();server.server_close();thread.join()
    (out/'results.json').write_text(json.dumps(dict(source_hashes=source_hashes,cases=results),indent=2)+'\n')
    print(f'PASS {len(results)} local TLS migration cases; historical git snapshots, per-hop allowlist, authenticated fallback, Range/hash and no external requests')


if __name__=='__main__':main()
