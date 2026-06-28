import os

def create_svgs():
    output_dir = "src/ui/images"
    os.makedirs(output_dir, exist_ok=True)

    # 1. DU (Dial Up) SVG
    du_content = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <!-- Case -->
  <rect x="20" y="45" width="60" height="14" rx="2" fill="none" stroke="#ffffff" stroke-width="4"/>
  <!-- Dial Glass (Top Arc) -->
  <path d="M 24 45 A 32 32 0 0 1 76 45" fill="none" stroke="#ffffff" stroke-width="3"/>
  <!-- Crown -->
  <rect x="80" y="48" width="5" height="8" rx="1" fill="#ffffff"/>
  <!-- Arrow indicating UP direction of Dial -->
  <path d="M 50 30 L 50 15 M 45 22 L 50 15 L 55 22" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
</svg>"""

    # 1-empty. DU (Dial Up) Empty SVG
    du_empty_content = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <!-- Empty Case Silhouette (Dashed) -->
  <rect x="20" y="45" width="60" height="14" rx="2" fill="none" stroke="#ffffff" stroke-width="3" stroke-dasharray="4,4"/>
  <!-- Empty Crown Outline -->
  <rect x="80" y="48" width="5" height="8" rx="1" fill="none" stroke="#ffffff" stroke-width="2"/>
</svg>"""

    # 2. DD (Dial Down) SVG
    dd_content = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <!-- Case -->
  <rect x="20" y="41" width="60" height="14" rx="2" fill="none" stroke="#ffffff" stroke-width="4"/>
  <!-- Dial Glass (Bottom Arc) -->
  <path d="M 24 55 A 32 32 0 0 0 76 55" fill="none" stroke="#ffffff" stroke-width="3"/>
  <!-- Crown -->
  <rect x="80" y="44" width="5" height="8" rx="1" fill="#ffffff"/>
  <!-- Arrow indicating DOWN direction of Dial -->
  <path d="M 50 70 L 50 85 M 45 78 L 50 85 L 55 78" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>
</svg>"""

    # 2-empty. DD (Dial Down) Empty SVG
    dd_empty_content = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <!-- Empty Case Silhouette (Dashed) -->
  <rect x="20" y="41" width="60" height="14" rx="2" fill="none" stroke="#ffffff" stroke-width="3" stroke-dasharray="4,4"/>
  <!-- Empty Crown Outline -->
  <rect x="80" y="44" width="5" height="8" rx="1" fill="none" stroke="#ffffff" stroke-width="2"/>
</svg>"""

    # Helper for front view watch (Common base dial)
    def get_dial_svg(crown_x, crown_y, crown_w, crown_h):
        return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <!-- Outer Casing -->
  <circle cx="50" cy="50" r="32" fill="none" stroke="#ffffff" stroke-width="4"/>
  <!-- Dial Ticks -->
  <line x1="50" y1="22" x2="50" y2="26" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
  <line x1="78" y1="50" x2="74" y2="50" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
  <line x1="50" y1="78" x2="50" y2="74" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
  <line x1="22" y1="50" x2="26" y2="50" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
  <!-- Clock Hands (approx. 10:10) -->
  <line x1="50" y1="50" x2="38" y2="38" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
  <line x1="50" y1="50" x2="68" y2="50" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
  <!-- Crown -->
  <rect x="{crown_x}" y="{crown_y}" width="{crown_w}" height="{crown_h}" rx="1.5" fill="#ffffff"/>
</svg>"""

    # Helper for empty front view watch holder
    def get_dial_empty_svg(crown_x, crown_y, crown_w, crown_h):
        return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <!-- Outer Ring (Dashed) -->
  <circle cx="50" cy="50" r="32" fill="none" stroke="#ffffff" stroke-width="3" stroke-dasharray="4,4"/>
  <!-- Empty Crown Outline -->
  <rect x="{crown_x}" y="{crown_y}" width="{crown_w}" height="{crown_h}" rx="1.5" fill="none" stroke="#ffffff" stroke-width="2"/>
</svg>"""

    # 3. CR (Crown Right / 12H) - Crown at 3 o'clock (right)
    cr_content = get_dial_svg(82, 44, 6, 12)
    cr_empty_content = get_dial_empty_svg(82, 44, 6, 12)

    # 4. CL (Crown Left / 6H) - Crown at 9 o'clock (left)
    cl_content = get_dial_svg(12, 44, 6, 12)
    cl_empty_content = get_dial_empty_svg(12, 44, 6, 12)

    # 5. CU (Crown Up / 3H) - Crown at 12 o'clock (top)
    cu_content = get_dial_svg(44, 12, 12, 6)
    cu_empty_content = get_dial_empty_svg(44, 12, 12, 6)

    # 6. CD (Crown Down / 9H) - Crown at 6 o'clock (bottom)
    cd_content = get_dial_svg(44, 82, 12, 6)
    cd_empty_content = get_dial_empty_svg(44, 82, 12, 6)

    # 7. Camera Disconnected SVG
    camera_disconnected_content = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <!-- Camera Body -->
  <rect x="25" y="35" width="50" height="35" rx="5" fill="none" stroke="#ffffff" stroke-width="4"/>
  <!-- Top Prism -->
  <path d="M 40 35 L 45 27 L 55 27 L 60 35 Z" fill="none" stroke="#ffffff" stroke-width="4" stroke-linejoin="round"/>
  <!-- Lens -->
  <circle cx="50" cy="52" r="11" fill="none" stroke="#ffffff" stroke-width="4"/>
  <!-- Diagonal Slash -->
  <line x1="20" y1="20" x2="80" y2="80" stroke="#ff7043" stroke-width="5" stroke-linecap="round"/>
</svg>"""

    svgs = {
        "pos_du.svg": du_content,
        "pos_du_empty.svg": du_empty_content,
        "pos_dd.svg": dd_content,
        "pos_dd_empty.svg": dd_empty_content,
        "pos_cr.svg": cr_content,
        "pos_cr_empty.svg": cr_empty_content,
        "pos_cl.svg": cl_content,
        "pos_cl_empty.svg": cl_empty_content,
        "pos_cu.svg": cu_content,
        "pos_cu_empty.svg": cu_empty_content,
        "pos_cd.svg": cd_content,
        "pos_cd_empty.svg": cd_empty_content,
        "pos_camera_disconnected.svg": camera_disconnected_content
    }

    for name, content in svgs.items():
        path = os.path.join(output_dir, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"Generated {path}")

if __name__ == "__main__":
    create_svgs()
