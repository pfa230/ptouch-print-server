#!/usr/bin/env python3
"""Run inside an isolated CUPS-equipped container, without USB or external network.

Usage: python3 scripts/check-cups.py /path/to/ptouch-app
Requires cupsd, cups-filters, Ghostscript, ipptool, and Pillow.
"""
import importlib.util, os, pathlib, subprocess, tempfile, time, socket, sys, re, urllib.request, urllib.parse, http.cookiejar
from PIL import Image, ImageDraw
spec=importlib.util.spec_from_file_location("smoke", str(pathlib.Path(__file__).with_name("smoke-image.py")))
smoke=importlib.util.module_from_spec(spec); spec.loader.exec_module(smoke)
run=smoke.run
with tempfile.TemporaryDirectory(prefix="cups-queue-") as temp:
    d=pathlib.Path(temp)
    reserved=socket.socket()
    reserved.bind(("127.0.0.1",0))
    sink_port=reserved.getsockname()[1]
    sink=None
    env=dict(os.environ, PTOUCH_DEVICE_URI=f"socket://127.0.0.1:{sink_port}", CUPS_SERVER="127.0.0.1:8631")
    os.environ["CUPS_SERVER"]=env["CUPS_SERVER"]
    conf=d/"cupsd.conf"
    conf.write_text("""LogLevel debug
Listen 127.0.0.1:8631
Browsing Off
WebInterface No
DefaultAuthType None
<Location />
  Order allow,deny
  Allow from localhost
</Location>
<Location /admin>
  Order allow,deny
  Allow from localhost
</Location>
<Policy default>
  <Limit All>
    Order allow,deny
    Allow from localhost
  </Limit>
</Policy>
""")
    app_log=open(d/"app.log", "w")
    cups_log=open(d/"cups.log", "w")
    app=subprocess.Popen([sys.argv[1], "server"], env=env, stdout=app_log, stderr=app_log)
    cups=subprocess.Popen(["cupsd", "-f", "-c", str(conf)], env=env, stdout=cups_log, stderr=cups_log)
    try:
        url="ipp://127.0.0.1:8000/ipp/print/ptouch"
        query=smoke.operation("Get-Printer-Attributes", "ATTR keyword requested-attributes all", "EXPECT finishings-default WITH-VALUE 11")
        for attempt in range(20):
            try:
                print(smoke.ipp(url,d,query)); break
            except RuntimeError:
                if attempt == 19:
                    raise
                time.sleep(1)
        # Existing Printing Defaults form must render and preserve canonical cuts.
        browser=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        weburl="http://127.0.0.1:8000/ptouch/printing"
        for default in (11,63):
            print(smoke.ipp(url,d,smoke.operation("Set-Printer-Attributes",
                "GROUP printer-attributes-tag\n ATTR enum finishings-default "+str(default),"")))
            html=browser.open(weburl).read().decode()
            token=re.search(r'name="session" value="([^"]+)"',html).group(1)
            form=urllib.parse.urlencode({"session":token,"print-quality":"normal"}).encode()
            html=browser.open(weburl,form).read().decode()
            assert "Changes saved." in html, html
            print(smoke.ipp(url,d,smoke.operation("Get-Printer-Attributes",
                "ATTR keyword requested-attributes finishings-default", "EXPECT finishings-default WITH-VALUE "+str(default))))
        print(smoke.ipp(url,d,smoke.operation("Set-Printer-Attributes",
            "GROUP printer-attributes-tag\n ATTR enum finishings-default 11","")))
        print(run("lpadmin", "-p", "ptouch-cups", "-E", "-v", url, "-m", "everywhere"))
        print(run("lpoptions", "-p", "ptouch-cups", "-o", "finishings=11"))
        print(run("lpoptions", "-p", "ptouch-cups", "-l"))
        png=d/"label.png"; smoke.png(png)
        Image.open(png).save(d/"label.jpg", dpi=(180,180), quality=95)
        pdf=d/"labels.pdf"
        stream1=b"1 w 3 3 28 125 re S 8 20 4 4 re f\n"
        stream2=b"1 w 3 3 28 125 re S 8 35 4 4 re f\n"
        objects=[b"<< /Type /Catalog /Pages 2 0 R >>", b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
                 b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 34.015748 141.732283] /Resources << >> /Contents 5 0 R >>",
                 b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 34.015748 141.732283] /Resources << >> /Contents 6 0 R >>",
                 b"<< /Length "+str(len(stream1)).encode()+b" >>\nstream\n"+stream1+b"endstream",
                 b"<< /Length "+str(len(stream2)).encode()+b" >>\nstream\n"+stream2+b"endstream"]
        data=bytearray(b"%PDF-1.4\n"); offsets=[0]
        for number,obj in enumerate(objects,1):
            offsets.append(len(data)); data.extend(str(number).encode()+b" 0 obj\n"+obj+b"\nendobj\n")
        start=len(data); data.extend(b"xref\n0 7\n0000000000 65535 f \n")
        for offset in offsets[1:]: data.extend(f"{offset:010d} 00000 n \n".encode())
        data.extend(f"trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n".encode()); pdf.write_bytes(data)
        # Two real CUPS jobs wait while the printer socket refuses connections.
        for finishing in ("60","63"):
            print(run("lp","-d","ptouch-cups","-o","media=om_12x50mm_12x50mm",
                      "-o","print-scaling=none","-o","finishings="+finishing,str(png)))
        time.sleep(1)
        waiting=run("lpstat","-o","ptouch-cups")
        assert waiting.count("ptouch-cups-")==2, waiting
        print("Offline jobs:",waiting)
        reserved.close()
        sink=smoke.Sink(sink_port)
        for number in range(2):
            result=smoke.summary(smoke.decode(sink.take(timeout=45)))
            assert len(result["pages"])==1 and result["pages"][0]["rows"]==354 and result["eject"]==1, result
            print("Resumed offline job",number+1,result)
        # PNG/JPEG and a multi-label PDF through an ordinary driverless queue.
        for file, finishing, count, copies in ((png,None,1,1),(png,"60",1,1),(d/"label.jpg","63",1,1),(pdf,"60",2,1),(pdf,"63",2,1),(pdf,"60",4,2)):
            args=["lp", "-d", "ptouch-cups", "-n", str(copies), "-o", "media=om_12x50mm_12x50mm", "-o", "print-scaling=none"]
            if finishing: args += ["-o", "finishings="+finishing]
            print(run(*args,str(file)))
            data=sink.take()
            result=smoke.summary(smoke.decode(data)); print("CUPS",file.name,finishing,result)
            if not result["pages"]:
                print(smoke.ipp(url,d,smoke.operation("Get-Job-Attributes", "ATTR integer job-id 1", "EXPECT job-state")))
            assert len(result["pages"])==count, result
            assert [p["rows"] for p in result["pages"]]==[354]*count, result
            assert result["eject"]==1, result
            assert [p["autocut"] for p in result["pages"]]==([True]*count if finishing!="63" else [True]+[False]*(count-1)), result
            assert not result["terminal_commands_after_pages"], result
        # A legacy CUPS PPD has one global custom margin, the maximum across
        # tapes. Honor it: a compact source fits; a full 76-dot source does not.
        custom=d/"custom.png"
        image=Image.new("L",(42,180),255)
        ImageDraw.Draw(image).rectangle((0,0,41,179),outline=0)
        image.save(custom,dpi=(180,180))
        print(run("lp","-d","ptouch-cups","-o","media=Custom.12x40mm",
                  "-o","print-scaling=none",str(custom)))
        result=smoke.summary(smoke.decode(sink.take(timeout=45)))
        assert len(result["pages"])==1 and result["pages"][0]["rows"]==282 and result["eject"]==1, result
        print("CUPS rounded custom size with explicit margins:",result)
        print(run("lp","-d","ptouch-cups","-o","media=Custom.12x40mm",
                  "-o","print-scaling=none",str(png)))
        assert sink.take(timeout=45)==b"", "Oversized custom PNG produced output"
        assert "PNG content is 76 dots across tape; printable width is 42" in (d/"app.log").read_text(), "Missing geometry rejection"
        print("CUPS oversized custom PNG rejected without output.")
        # Exact custom dimensions and tape margins use ordinary media-col.
        request=smoke.operation("Print-Job", f"""ATTR mimeMediaType document-format image/png
          GROUP job-attributes-tag
          ATTR keyword print-scaling none
          ATTR collection media-col {{
            MEMBER collection media-size {{ MEMBER integer x-dimension 1200 MEMBER integer y-dimension 4000 }}
            MEMBER integer media-left-margin 63
            MEMBER integer media-right-margin 71
          }}
          FILE "{png}"
          """, "EXPECT job-id WITH-VALUE >0 DEFINE-VALUE job-id")
        request+=smoke.operation("Get-Job-Attributes", "ATTR integer job-id $job-id",
            "EXPECT job-state WITH-VALUE >5 REPEAT-NO-MATCH REPEAT-LIMIT 50\n EXPECT job-state WITH-VALUE 9")
        print(smoke.ipp(url,d,request))
        result=smoke.summary(smoke.decode(sink.take()))
        assert len(result["pages"])==1 and result["pages"][0]["rows"]==283 and result["eject"]==1, result
        print("Exact standard custom media-col:",result)
        # Chain-only travels through the standard IPP collection interface.
        print(smoke.submit(url,d,png,"ATTR collection finishings-col { MEMBER keyword finishing-template ptouch-chain }"))
        result=smoke.summary(smoke.decode(sink.take()))
        assert len(result["pages"])==1 and result["pages"][0]["rows"]==354 and result["eject"]==0, result
        assert result["pages"][0]["autocut"], result
        print("Standard ptouch-chain collection:",result)
        print("Conventional CUPS integration passed.")
    finally:
        for process in (cups,app):
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        reserved.close()
        if sink: sink.socket.close()
        app_log.close(); cups_log.close()
        print((d/"app.log").read_text(errors="replace"))
        print((d/"cups.log").read_text(errors="replace"))
        if sys.exc_info()[0] and pathlib.Path("/var/log/cups/error_log").exists():
            print(pathlib.Path("/var/log/cups/error_log").read_text(errors="replace"))
