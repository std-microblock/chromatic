<div align=center>
<img src=https://github.com/user-attachments/assets/4e0d0de3-c117-44e0-aed5-f95eaac33ba7 width=200/>
<h1>chromatic</h1>
<h5>Universal modifier for Chromium/V8 | 广谱注入 Chromium/V8 的通用修改器</h5>  
</div>

> [!NOTE]
> 在找 BetterNCM ？
> 
> 由于作者迁移至 QQ 音乐，BetterNCM 疏于维护，以及其年代已久，现将相关代码整体重写并支持极多其它软件，改名为 chromatic。
>
> 点击以查看 [BetterNCM 相关代码存档](https://github.com/std-microblock/chromatic/tree/v2) 或 [最后一版 BetterNCM Release](https://github.com/std-microblock/chromatic/releases/tag/1.3.4)

## Showcase

```javascript
import { chrome } from "chromatic"

chrome.blink.add_blink_parse_html_manipulator(html => {
    if (html.includes('<body')) {
        return html.replace("<body", `
           <div style="
          position: fixed;
          left: 13px;
          top: 11px;
          background: #00000022;
          color: white;
          z-index: 9999;
          backdrop-filter: blur(20px);
          padding: 10px 20px;
          font-size: 15px;
          border-radius: 100px;
          overflow: hidden;
          border: 1px solid #00000038;
          font-family: Consolas;
          cursor: pointer;
      " onclick="location.reload()">Chromatic</div>
      <body
            `)
    }
})
```

![image](https://github.com/user-attachments/assets/6d72958e-d673-4c80-bcd3-e7da743479e3)
