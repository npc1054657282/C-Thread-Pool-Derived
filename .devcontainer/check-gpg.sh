#!/usr/bin/env bash

if (! git config commit.gpgsign >/dev/null 2>&1;) && (! git config --global commit.gpgsign >/dev/null 2>&1;) then
  cat << 'EOF'

🚨 **Reminder:**  
To ensure your PR isn’t blocked, please:

  1. Go to **Settings → Codespaces → GPG verification** and **Enable** it.  
  2. Under **Trusted repositories**, add this repository.  
  3. **Stop & Restart** your Codespace so the new setting takes effect.

EOF
else echo -e "\n✅ Git Commit GPG Sign is enabled.\n"
fi