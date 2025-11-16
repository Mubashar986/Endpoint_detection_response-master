"""
Django management command to load production detection rules.

This file contains battle-tested rules for:
- PowerShell attacks (command-line based, works with your agent)
- Office macro attacks (using parent_image field)
- Suspicious network connections
- Process execution anomalies

Usage:
    python manage.py seed_rules

Note: Rules are tuned to minimize false positives based on real testing.
"""

from django.core.management.base import BaseCommand
from ingestion.detection_models import DetectionRule


class Command(BaseCommand):
    help = 'Load production detection rules into database'
    
    def handle(self, *args, **options):
        self.stdout.write("🔧 Loading detection rules...")
        
        rules = [
            # ========== RULE 1: Basic PowerShell Detection ==========
            {
                'rule_id': 'RULE-PS-001',
                'name': 'PowerShell Execution Detected',
                'description': 'Detects any PowerShell execution for monitoring purposes',
                'enabled': True,
                'severity': 'LOW',
                'confidence': 0.70,
                'mitre_tactics': ['TA0002'],
                'mitre_techniques': ['T1059.001'],
                'mitre_tactic_names': ['Execution'],
                'mitre_technique_names': ['PowerShell'],
                'tags': ['powershell', 'monitoring', 'execution'],
                'detection_logic': {
                    'entity_type': 'process',
                    'conditions': [
                        {
                            'field': 'process.command_line',  # Uses command_line (works with your agent)
                            'operator': 'contains',
                            'value': 'powershell',
                            'case_sensitive': False
                        }
                    ],
                    'logic': 'AND'
                },
                'exceptions': [
                    {
                        'field': 'process.user',
                        'operator': 'equals',
                        'value': 'NT AUTHORITY\\SYSTEM',
                        'reason': 'System-level PowerShell tasks'
                    }
                ],
                'author': 'security-team',
                'deployment_status': 'PRODUCTION'
            },
            
            # ========== RULE 2: Suspicious Encoded PowerShell ==========
            {
                'rule_id': 'RULE-PS-002',
                'name': 'Suspicious Encoded PowerShell',
                'description': 'Detects PowerShell with encoded/obfuscated commands (malware indicator)',
                'enabled': True,
                'severity': 'CRITICAL',
                'confidence': 0.95,
                'mitre_tactics': ['TA0002', 'TA0005'],
                'mitre_techniques': ['T1059.001', 'T1027'],
                'mitre_tactic_names': ['Execution', 'Defense Evasion'],
                'mitre_technique_names': ['PowerShell', 'Obfuscated Files or Information'],
                'tags': ['powershell', 'obfuscation', 'malware', 'critical'],
                'detection_logic': {
                    'entity_type': 'process',
                    'conditions': [
                        {
                            'field': 'process.command_line',
                            'operator': 'contains',
                            'value': 'powershell',  # Must be PowerShell
                            'case_sensitive': False
                        },
                        {
                            'field': 'process.command_line',
                            'operator': 'contains_any',  # AND suspicious flags
                            'values': [
                                '-EncodedCommand',
                                'FromBase64String',
                                'DownloadString',
                                'Invoke-Expression',
                                'IEX(',
                                '-WindowStyle Hidden',
                                '-NonInteractive'
                            ],
                            'case_sensitive': False
                        }
                    ],
                    'logic': 'AND'  # BOTH conditions required (prevents Git false positives)
                },
                'exceptions': [
                    {
                        'field': 'process.user',
                        'operator': 'equals',
                        'value': 'NT AUTHORITY\\SYSTEM',
                        'reason': 'System PowerShell automation'
                    }
                ],
                'author': 'security-team',
                'deployment_status': 'PRODUCTION'
            },
            
            # ========== RULE 3: Office Macro Attack ==========
            {
                'rule_id': 'RULE-MACRO-001',
                'name': 'Office Application Spawned Shell',
                'description': 'Detects Office apps spawning command shells (macro attack indicator)',
                'enabled': True,
                'severity': 'CRITICAL',
                'confidence': 0.90,
                'mitre_tactics': ['TA0002', 'TA0001'],
                'mitre_techniques': ['T1059.003', 'T1566.001'],
                'mitre_tactic_names': ['Execution', 'Initial Access'],
                'mitre_technique_names': ['Windows Command Shell', 'Spearphishing Attachment'],
                'tags': ['macro', 'office', 'phishing', 'critical'],
                'detection_logic': {
                    'entity_type': 'process',
                    'conditions': [
                        {
                            'field': 'process.command_line',
                            'operator': 'contains_any',
                            'values': ['cmd.exe', 'powershell', 'wscript', 'cscript'],
                            'case_sensitive': False
                        },
                        {
                            'field': 'process.parent_image',  # Your agent uses parent_image
                            'operator': 'contains_any',
                            'values': [
                                'WINWORD.EXE', 'EXCEL.EXE', 'POWERPNT.EXE',
                                'OUTLOOK.EXE', 'MSACCESS.EXE'
                            ],
                            'case_sensitive': False
                        }
                    ],
                    'logic': 'AND'
                },
                'exceptions': [],
                'author': 'security-team',
                'deployment_status': 'PRODUCTION'
            },
            
            # ========== RULE 4: Suspicious Network Connection ==========
            {
                'rule_id': 'RULE-NET-001',
                'name': 'Suspicious High Port Connection',
                'description': 'Detects connections to non-standard high ports (C2 indicator)',
                'enabled': True,
                'severity': 'MEDIUM',
                'confidence': 0.65,
                'mitre_tactics': ['TA0011'],
                'mitre_techniques': ['T1071.001'],
                'mitre_tactic_names': ['Command and Control'],
                'mitre_technique_names': ['Application Layer Protocol'],
                'tags': ['network', 'c2', 'command-and-control'],
                'detection_logic': {
                    'entity_type': 'network',
                    'conditions': [
                        {
                            'field': 'network.dest_port',
                            'operator': 'greater_than',
                            'value': 8000
                        },
                        {
                            'field': 'network.protocol',
                            'operator': 'equals',
                            'value': 'tcp',  # Lowercase (your agent format)
                            'case_sensitive': False
                        }
                    ],
                    'logic': 'AND'
                },
                'exceptions': [
                    {
                        'field': 'network.image',  # Your agent uses 'image' field
                        'operator': 'contains_any',
                        'values': [
                            'chrome.exe', 'firefox.exe', 'msedge.exe',
                            'teams.exe', 'slack.exe', 'discord.exe',
                            'Perplexity.exe'  # Added your app
                        ],
                        'reason': 'Legitimate browsers and communication apps'
                    }
                ],
                'author': 'security-team',
                'deployment_status': 'PRODUCTION'
            },
            
            # ========== RULE 5: Credential Dumping Tools ==========
            {
                'rule_id': 'RULE-CRED-001',
                'name': 'Credential Dumping Tool Detected',
                'description': 'Detects known credential theft tools (mimikatz, procdump, etc.)',
                'enabled': True,
                'severity': 'CRITICAL',
                'confidence': 0.98,
                'mitre_tactics': ['TA0006'],
                'mitre_techniques': ['T1003.001'],
                'mitre_tactic_names': ['Credential Access'],
                'mitre_technique_names': ['LSASS Memory'],
                'tags': ['credential-theft', 'mimikatz', 'critical'],
                'detection_logic': {
                    'entity_type': 'process',
                    'conditions': [
                        {
                            'field': 'process.command_line',
                            'operator': 'contains_any',
                            'values': [
                                'mimikatz', 'sekurlsa::logonpasswords',
                                'procdump', 'lsass', 'pwdump',
                                'gsecdump', 'wce.exe'
                            ],
                            'case_sensitive': False
                        }
                    ],
                    'logic': 'AND'
                },
                'exceptions': [],
                'author': 'security-team',
                'deployment_status': 'PRODUCTION'
            },
            
            # ========== RULE 6: Suspicious File Execution ==========
            {
                'rule_id': 'RULE-FILE-001',
                'name': 'Suspicious File Execution Location',
                'description': 'Detects processes running from temp/download folders (common malware location)',
                'enabled': True,
                'severity': 'HIGH',
                'confidence': 0.75,
                'mitre_tactics': ['TA0002'],
                'mitre_techniques': ['T1204.002'],
                'mitre_tactic_names': ['Execution'],
                'mitre_technique_names': ['Malicious File'],
                'tags': ['malware', 'downloads', 'temp'],
                'detection_logic': {
                    'entity_type': 'process',
                    'conditions': [
                        {
                            'field': 'process.command_line',
                            'operator': 'contains_any',
                            'values': [
                                '\\Downloads\\',
                                '\\Temp\\',
                                '\\AppData\\Local\\Temp\\',
                                'C:\\Users\\Public\\'
                            ],
                            'case_sensitive': False
                        }
                    ],
                    'logic': 'AND'
                },
                'exceptions': [
                    {
                        'field': 'process.command_line',
                        'operator': 'contains_any',
                        'values': [
                            'chrome.exe', 'firefox.exe', 'windowsupdate',
                            'installer', 'setup.exe'
                        ],
                        'reason': 'Legitimate installers and updates'
                    }
                ],
                'author': 'security-team',
                'deployment_status': 'PRODUCTION'
            }
        ]
        
        created_count = 0
        updated_count = 0
        skipped_count = 0
        
        for rule_data in rules:
            try:
                # Check if rule exists
                existing = DetectionRule.objects.filter(rule_id=rule_data['rule_id']).first()
                
                if existing:
                    # Ask if should update
                    self.stdout.write(f"⚠️  Rule {rule_data['rule_id']} already exists")
                    
                    # Update existing rule
                    for key, value in rule_data.items():
                        setattr(existing, key, value)
                    existing.save()
                    self.stdout.write(self.style.WARNING(f"   ✅ Updated: {rule_data['rule_id']}"))
                    updated_count += 1
                else:
                    # Create new rule
                    rule = DetectionRule(**rule_data)
                    rule.save()
                    self.stdout.write(self.style.SUCCESS(f"✅ Created: {rule_data['rule_id']} - {rule_data['name']}"))
                    created_count += 1
                    
            except Exception as e:
                self.stdout.write(self.style.ERROR(f"❌ Failed to load {rule_data['rule_id']}: {e}"))
                skipped_count += 1
        
        # Summary
        self.stdout.write("\n" + "=" * 60)
        self.stdout.write(self.style.SUCCESS(f"✅ Rule Loading Complete!"))
        self.stdout.write(f"   Created: {created_count} new rules")
        self.stdout.write(f"   Updated: {updated_count} existing rules")
        if skipped_count > 0:
            self.stdout.write(self.style.WARNING(f"   Skipped: {skipped_count} rules (errors)"))
        self.stdout.write("=" * 60)
