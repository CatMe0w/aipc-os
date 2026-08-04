export type Lang = "zh" | "en";

export type DriverStatus = "ok" | "wip" | "no";
export type RoadmapStatus = "ok" | "wip" | "pending";

export interface Bilingual {
  zh: string;
  en: string;
}

export interface Driver extends Bilingual {
  id: string;
  status: DriverStatus;
}

export interface RoadmapItem {
  id: string;
  status: RoadmapStatus;
  zh: { label: string; desc: string };
  en: { label: string; desc: string };
}

export interface Flavor {
  id: "warm" | "cold";
  accent: "amber" | "accent";
  zh: { lead: string; title: string; body: string[] };
  en: { lead: string; title: string; body: string[] };
}

export interface HeroSub {
  t: string;
  hl?: boolean;
}

export interface NavLink {
  href: string;
  label: string;
}

export interface PageContent {
  head: { title: string; description: string };
  nav: { site: string; links: NavLink[] };
  hero: {
    logo: string;
    tagline: string;
    sub: HeroSub[];
    cta: string;
    ctaHref: string;
    langHref: string;
    langLabel: string;
  };
  summary: { eyebrow: string; title: string; paragraphs: string[] };
  roadmap: { eyebrow: string; title: string };
  hardware: { eyebrow: string; title: string };
  omt: { eyebrow: string; title: string; lead: string };
  footer: { prefix: string; href: string; label: string };
}

export const statusLabel: Record<DriverStatus, Bilingual & { dot: string; cls: string }> = {
  ok: { zh: "可用", en: "working", dot: "●", cls: "ds-ok" },
  wip: { zh: "进行中", en: "in progress", dot: "●", cls: "ds-wip" },
  no: { zh: "未开始", en: "not started", dot: "○", cls: "ds-no" },
};

export const drivers: Driver[] = [
  { id: "arm926", status: "ok", zh: "ARM926EJ-S核心", en: "ARM926EJ-S core" },
  { id: "mmu", status: "ok", zh: "MMU", en: "MMU" },
  { id: "timers", status: "ok", zh: "定时器", en: "Timers" },
  { id: "clocks", status: "ok", zh: "时钟 / PLL", en: "Clocks / PLL" },
  { id: "display", status: "ok", zh: "屏幕显示", en: "Display" },
  { id: "uart", status: "ok", zh: "UART", en: "UART" },
  { id: "usb-ch374", status: "ok", zh: "USB（外挂控制器）", en: "USB (CH374)" },
  { id: "spi", status: "ok", zh: "SPI控制器", en: "SPI controller" },
  { id: "keyboard", status: "ok", zh: "键盘", en: "Keyboard" },
  { id: "ethernet", status: "ok", zh: "以太网", en: "Ethernet" },
  { id: "sdmmc", status: "ok", zh: "SD / MMC", en: "SD / MMC" },
  { id: "reset", status: "wip", zh: "复位管理", en: "Reset management" },
  { id: "power", status: "wip", zh: "电源管理", en: "Power management" },
  { id: "wifi", status: "wip", zh: "Wi-Fi", en: "Wi-Fi" },
  { id: "touchpad", status: "wip", zh: "触摸板", en: "Touchpad" },
  { id: "nand", status: "no", zh: "NAND Flash", en: "NAND Flash" },
  { id: "i2c", status: "no", zh: "I2C控制器", en: "I2C controller" },
  { id: "usb-soc", status: "no", zh: "USB（SoC）", en: "USB (SoC-native)" },
  { id: "backlight", status: "no", zh: "背光控制", en: "Backlight" },
  { id: "battery", status: "no", zh: "电池电量监测", en: "Battery monitor" },
  { id: "rtc", status: "no", zh: "RTC", en: "RTC" },
  { id: "audio", status: "no", zh: "音频", en: "Audio" },
  { id: "gpu2d", status: "no", zh: "2D图形加速", en: "2D GPU" },
  { id: "videodec", status: "no", zh: "视频硬解码", en: "Video decode" },
];

export const roadmap: RoadmapItem[] = [
  {
    id: "early-kernel",
    status: "ok",
    zh: { label: "早期内核启动", desc: "初步device tree完工，Linux 7.0-rc3已成功引导并进入shell，屏幕与UART已点亮。" },
    en: { label: "Early kernel boot", desc: "Initial device tree complete. Linux 7.0-rc3 boots to a shell. Display and UART are functional." },
  },
  {
    id: "gpio-spi-usb",
    status: "ok",
    zh: { label: "GPIO、SPI与USB驱动", desc: "驱动内置键盘的前置条件，也是标志着Linux在AIPC真正可用的里程碑。" },
    en: {
      label: "GPIO, SPI, and USB drivers",
      desc: "A prerequisite for keyboard support, and the milestone that marks Linux as genuinely usable on the AIPC.",
    },
  },
  {
    id: "userland",
    status: "wip",
    zh: { label: "Userland与桌面", desc: "AIPC仅配备64MB内存，我们需要打造自己的发行版体系。" },
    en: { label: "Userland and desktop", desc: "With only 64 MB of RAM, a custom distribution stack is needed rather than an off-the-shelf base." },
  },
  {
    id: "release",
    status: "pending",
    zh: { label: "镜像正式发布", desc: "发布首个可引导镜像，将配备原厂系统的全部或绝大部分功能：浏览器、Office套件、PDF阅读器、视频播放器等。" },
    en: {
      label: "First public image",
      desc: "Release the first bootable image covering all or most of the original firmware's functionality: browser, office suite, PDF reader, video player, and more.",
    },
  },
  {
    id: "future",
    status: "pending",
    zh: { label: "展望未来", desc: "重新编写引导程序、2D图形加速、触屏改造……" },
    en: { label: "Further work", desc: "Custom bootloader, 2D GPU acceleration, touchscreen hardmod..." },
  },
];

export const flavors: Flavor[] = [
  {
    id: "warm",
    accent: "amber",
    zh: { lead: "Warmboot", title: "从Windows CE内引导", body: ["将文件拷贝到SD卡，在原厂Windows CE固件下以普通程序方式执行，由HaRET完成剩余的工作。"] },
    en: {
      lead: "Warmboot",
      title: "Boot from within Windows CE",
      body: ["Copy the files to an SD card and run the launcher from the stock Windows CE firmware.<br>HaRET handles the rest."],
    },
  },
  {
    id: "cold",
    accent: "accent",
    zh: { lead: "Coldboot", title: "绕过Windows CE，直接引导", body: ["无需经过Windows CE，上电即可进入AIPC OS。<br>还可实现WinCE + Linux双系统。"] },
    en: {
      lead: "Coldboot",
      title: "Boot directly, bypassing Windows CE",
      body: ["Power on and boot straight into AIPC OS, with no pass through Windows CE.<br>Also supports WinCE + Linux dual-boot."],
    },
  },
];

export const content: Record<Lang, PageContent> = {
  zh: {
    head: {
      title: "AIPC OS | 经久不衰的鬼畜素材，历久弥新的掌上电脑",
      description: "为电视广告“三十秒开机，三秒死机”的复古电脑带来Linux 7.x",
    },
    nav: {
      site: "catme0w.org",
      links: [
        { href: "#summary", label: "关于" },
        { href: "#roadmap", label: "路线图" },
        { href: "#hardware", label: "硬件" },
        { href: "#omt", label: "展望" },
      ],
    },
    hero: {
      logo: "AIPC OS",
      tagline: "经久不衰的鬼畜素材，历久弥新的掌上电脑",
      sub: [{ t: "为电视广告" }, { t: "“三十秒开机，三秒死机”", hl: true }, { t: "的复古电脑带来Linux 7.x" }],
      cta: "GitHub",
      ctaHref: "https://github.com/CatMe0w/aipc-os",
      langHref: "/en",
      langLabel: "English version",
    },
    summary: {
      eyebrow: "// about",
      title: "一代人的回忆",
      paragraphs: [
        "AIPC，一种曾在电视购物广告上推销的手持电脑，由于其广告宣传语中包含大量虚假宣传以及毫无常识的夸张表达，一经播出即成为经久不衰的鬼畜素材。",
        "17年来，人们对AIPC的认知大多停留在鬼畜视频的解构中，却鲜有人真正触及它的底层架构。为此，我们启动了AIPC OS项目。我们的目标是在这台曾经的电子垃圾上，移植完整的Linux 7.x主线内核，将其锻造成真正符合手持电脑之名的极客工具。",
        "在海外，相同模具的手持电脑在同一时期大量出货，通常以Sylvania品牌的上网本出现，已有前人完成过Linux的适配。然而，纵使CPU核心相同，AIPC使用了不同的SoC与固件，既无主线支持，也无法直接从USB引导，而这正是我们工作的意义所在。",
      ],
    },
    roadmap: { eyebrow: "// roadmap", title: "我们在哪里" },
    hardware: { eyebrow: "// hardware", title: "硬件规格及移植进度" },
    omt: { eyebrow: "// one more thing", title: "远期规划", lead: "AIPC OS将提供两种版本。" },
    footer: { prefix: "with 💖 from", href: "https://catme0w.org", label: "catme0w" },
  },
  en: {
    head: {
      title: "AIPC OS | The joke hardware. A serious OS.",
      description: "Bringing Linux 7.x to a vintage handheld the internet summarized as: thirty-second boot. Three-second uptime.",
    },
    nav: {
      site: "catme0w.org",
      links: [
        { href: "#summary", label: "About" },
        { href: "#roadmap", label: "Roadmap" },
        { href: "#hardware", label: "Hardware" },
        { href: "#omt", label: "Future" },
      ],
    },
    hero: {
      logo: "AIPC OS",
      tagline: "The joke hardware. A serious OS.",
      sub: [{ t: "Bringing Linux 7.x to a vintage handheld the internet summarized as: " }, { t: "thirty-second boot. Three-second uptime.", hl: true }],
      cta: "GitHub",
      ctaHref: "https://github.com/CatMe0w/aipc-os",
      langHref: "/",
      langLabel: "中文版本",
    },
    summary: {
      eyebrow: "// about",
      title: "What Is AIPC",
      paragraphs: [
        "AIPC was a handheld computer sold in China through television shopping channels around 2009. Its advertisements became a lasting subject of internet mockery - the claims were extravagant, the hardware was not, and the gap between the two was comedic enough to outlive the product by over a decade.",
        'For seventeen years, the AIPC has existed almost entirely as a meme. Its actual hardware has received almost no serious technical attention: the SoC is undocumented, and no mainline kernel support exists. The AIPC OS project is an attempt to change that. The goal is a complete port of the Linux 7.x mainline kernel to the Anyka AK7802 SoC - turning a piece of discarded novelty hardware into a working, hackable platform that finally earns the name "handheld computer."',
        "Outside China, the same mold was used for a family of netbooks sold under the Sylvania brand. Those machines use a different SoC that already has mainline Linux support and can boot directly from USB. The AIPC has neither. That gap is what this project exists to close.",
      ],
    },
    roadmap: { eyebrow: "// roadmap", title: "Where things stand" },
    hardware: { eyebrow: "// hardware", title: "Hardware and driver status" },
    omt: { eyebrow: "// one more thing", title: "Future plans", lead: "Two boot flavors are planned for AIPC OS." },
    footer: { prefix: "with 💖 from", href: "https://catme0w.org", label: "catme0w" },
  },
};
