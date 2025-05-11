#!/usr/bin/env bash

script_content=$(cat <<EOF
if (! git config commit.gpgsign >/dev/null 2>&1;) && (! git config --global commit.gpgsign >/dev/null 2>&1;) then
  echo -e "\n🚨 **Reminder:**  \n\nTo ensure your PR isn’t blocked, please:\n  1. Go to **Settings → Codespaces → GPG verification** and **Enable** it.  \n  2. Under **Trusted repositories**, add this repository.\n  3. **Stop & Restart** your Codespace so the new setting takes effect.\n"
else
  echo -e "\n✅ Git Commit GPG Sign is enabled.\n"
fi
EOF
)
echo "$script_content" >> ~/.bashrc