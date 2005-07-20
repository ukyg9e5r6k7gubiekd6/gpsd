(defun statetable-regen ()
  "Turn the packet-getter's enum list into a string array."
  (interactive)
  (goto-char (point-min))
  (re-search-forward "enum \\({[^}]*};\\)")
  (let ((enums (buffer-substring (match-beginning 1) (match-end 1))))
    (if (re-search-forward "/\\*%start%\\*/\\([^%]*\\)/\\*%end%\\*/" nil t)
	(let ((start (save-excursion (goto-char (match-beginning 1)) (point-marker)))
	      (end (save-excursion (goto-char (match-end 1)) (point-marker))))
	  (replace-match (concat 
			"char *state_table[] = " 
		       enums
		       "\n\t") t t nil 1)
	  (goto-char start)
	  (while (re-search-forward "^ *\\([A-Z0-9_]+\\)," end t)
	    (replace-match 
	     (concat "\"" (buffer-substring (match-beginning 1) (match-end 1)) "\"")
	     t t nil 1))))))
      
