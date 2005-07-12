(defun statetable-regen ()
  "Turn the packet-getter's enum list into a string array."
  (goto-char (point-min))
  (search-forward "enum {")
  (narrow-to-region (point) (save-excursion (search-forward "}")))
  (let ((matches "\n\tchar *state_table[] = {\n"))
    (prog1
	(while (re-search-forward "^ +\\([A-Za-z0-9_]+\\)," nil t)
	  (let ((id (buffer-substring (match-beginning 1) (match-end 1))))
	    (setq matches (concat matches (format "\t\"%s\",\n" id))))))
    (widen)
    (if (re-search-forward "/\\*%start%\\*/\\([^%]*\\)/\\*%end%\\*/" nil t)
	(replace-match (concat matches "\t};\n\t") t t nil 1))
))
      
